#include "Include/Engine.hlsli"

// DefaultLit fragment stage: metallic-roughness PBR (Cook-Torrance) + normal mapping + directional
// shadows + split-sum IBL, then exposure/ACES tonemap/sRGB encode. Paired with the shared
// Mesh.vert.hlsl. Was the fragment half of the old combined DefaultLit.hlsl.

static const float PI = 3.14159265359;

// Sample a bindless texture by a (potentially per-instance, non-uniform) index. Every dynamic index
// into the Textures[] array must go through NonUniformResourceIndex() or instanced draws sample
// garbage (the #46 flicker lesson) — centralize it here so no call site forgets.
float4 SampleBindless(uint index, float2 uv)
{
	// MipBias (FrameCB) is negative under TAA so jitter fetches a sharper mip each frame — the temporal
	// resolve then reconstructs the detail instead of thin/distant texels flickering between mips. 0 = off.
	return Textures[NonUniformResourceIndex(index)].SampleBias(LinearSampler, uv, MipBias);
}

// Shared shadow factor: 1 = fully lit, 0 = fully shadowed. Reprojects the world position through a light
// matrix, then does a 3x3 PCF compare against a bindless depth texture. `atlasRect` (xy = UV offset,
// zw = UV scale) maps the light's [0,1] UV into its sub-rect of the texture: (0,0,1,1) for a dedicated
// map (the sun), or a tile rect for a spot in the shared atlas. PCF taps are CLAMPED to the rect so a
// tap near a tile edge can't bleed into a neighbour tile. Manual PCF keeps the bindless SAMPLED_IMAGE
// model (no comparison-sampler descriptor). NdotL drives a slope-scaled bias; ShadowStrength lightens.
float SampleShadowFactor(uint texIndex, float4x4 lightViewProj, float4 atlasRect, float3 positionWS, float NdotL)
{
	float4 lightClip = mul(float4(positionWS, 1.0), lightViewProj);
	float3 ndc = lightClip.xyz / lightClip.w;

	// Behind the light or outside its depth range => treat as lit. (w<=0 guards points behind a
	// perspective spot, where the divide flips.)
	if (lightClip.w <= 0.0 || ndc.z > 1.0 || ndc.z < 0.0)
	{
		return 1.0;
	}

	// Clip XY [-1,1] -> light UV [0,1]. NO Y-flip: the engine's SetViewport does NOT apply the Vulkan
	// negative-height flip, so the whole renderer is internally consistent in un-flipped clip space.
	float2 lightUV = ndc.xy * 0.5 + 0.5;
	if (lightUV.x < 0.0 || lightUV.x > 1.0 || lightUV.y < 0.0 || lightUV.y > 1.0)
	{
		return 1.0; // outside this light's frustum footprint
	}

	// Depth bias: constant floor + slope-scaled term (more bias on surfaces grazing the light). The floor
	// matters even for light-facing surfaces (texel quantization causes acne there too).
	const float bias = ShadowBias + ShadowBias * 4.0 * (1.0 - NdotL);
	const float currentDepth = ndc.z - bias;

	// Atlas remap + tile clamp bounds (in atlas UV space). ShadowTexelSize is per full-texture texel;
	// scale by the rect so a tile's PCF step matches its sub-resolution.
	const float2 rectMin = atlasRect.xy;
	const float2 rectMax = atlasRect.xy + atlasRect.zw;

	float visibility;
	if (ShadowSoft != 0)
	{
		float sum = 0.0;
		[unroll] for (int dy = -1; dy <= 1; ++dy)
		{
			[unroll] for (int dx = -1; dx <= 1; ++dx)
			{
				const float2 tap = lightUV + float2(dx, dy) * ShadowTexelSize;
				const float2 atlasUV = clamp(atlasRect.xy + tap * atlasRect.zw, rectMin, rectMax);
				const float storedDepth = SampleBindless(texIndex, atlasUV).r;
				sum += (currentDepth <= storedDepth) ? 1.0 : 0.0;
			}
		}
		visibility = sum / 9.0;
	}
	else
	{
		const float2 atlasUV = clamp(atlasRect.xy + lightUV * atlasRect.zw, rectMin, rectMax);
		const float storedDepth = SampleBindless(texIndex, atlasUV).r;
		visibility = (currentDepth <= storedDepth) ? 1.0 : 0.0;
	}

	return lerp(1.0, visibility, ShadowStrength);
}

#ifdef SS_RAYTRACING
// Ray-traced shadow (#118): trace an inline ray-query shadow ray from the surface toward a light against
// the scene TLAS. Any opaque hit before the ray reaches the light (tMax) => shadowed. `L` is the
// normalized direction TO the light; `Ng` is the geometric normal used to offset the ray origin off the
// surface (normal + slight light-dir push), which avoids self-intersection acne without a depth-space
// bias. `tMax` is the ray length: 1e30 for the sun (at infinity), or the distance to a local light minus
// a small epsilon (so the ray doesn't hit geometry at/behind the light itself). Returns
// lerp(1, visibility, ShadowStrength) to match the raster path's strength dial. RT permutation only.
float RayTraceShadow(float3 positionWS, float3 Ng, float3 L, float tMax)
{
	// Normal-offset the origin so the ray starts just off the surface; a small along-L push further guards
	// grazing angles. Scaled by 1e-2 world units — tuned against acne/peter-panning.
	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = L;
	ray.TMin = 0.0;
	ray.TMax = tMax;

	// ACCEPT_FIRST_HIT_AND_END_SEARCH: a shadow ray only needs "is anything in the way", so stop at the
	// first opaque hit. Geometry is built OPAQUE (no any-hit), so CULL_NON_OPAQUE is a no-op safety net.
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();

	const float visibility = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
	return lerp(1.0, visibility, ShadowStrength);
}

// Shadow rays per light per frame for the soft path. Compile-time (a cost-class knob, not a live one) —
// kept low because per-frame sample rotation + TAA accumulate many effective samples over time.
#define SHADOW_RAY_COUNT 2

// Soft ray-traced shadow (#118): like RayTraceShadow, but instead of one ray straight at the light, shoot
// SHADOW_RAY_COUNT rays whose directions are jittered within a disk of radius `coneRadius` (tan of the
// light's angular half-size) perpendicular to `L` — modelling the light's AREA. Averaging the hits gives a
// visibility in [0,1] (a penumbra) instead of {0,1}. Sharp where the caster is close (small subtended
// angle), softening with distance — the physical behaviour. Reuses the RTAO disk-sample + orthonormal
// basis + frame-rotated IGN hash so successive frames pick different directions and TAA smooths the noise.
// coneRadius == 0 reduces exactly to the hard single ray. RT permutation only.
float RayTraceSoftShadow(float3 positionWS, float3 Ng, float3 L, float tMax, float coneRadius, float2 pixelPos)
{
	// Orthonormal basis around the light direction L to place disk offsets in the plane perpendicular to it.
	const float3 up = abs(L.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, L));
	const float3 bitangent = cross(L, tangent);

	// Per-pixel + per-frame rotation seed (same interleaved-gradient-noise hash RTAO uses).
	const float2 px = pixelPos + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;

	float visSum = 0.0;
	[unroll] for (int s = 0; s < SHADOW_RAY_COUNT; ++s)
	{
		// Uniform disk sample (stratified by ray index, jittered by the hash), scaled to the cone radius.
		const float u1 = frac((float(s) + ign) / float(SHADOW_RAY_COUNT));
		const float u2 = frac(ign + float(s) * 0.61803398875); // golden-ratio decorrelation
		const float rr = coneRadius * sqrt(u1);
		const float phi = 2.0 * PI * u2;
		const float3 dir = normalize(L + (rr * cos(phi)) * tangent + (rr * sin(phi)) * bitangent);

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = dir;
		ray.TMin = 0.0;
		ray.TMax = tMax;

		RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		q.Proceed();

		visSum += (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
	}

	const float visibility = visSum / float(SHADOW_RAY_COUNT);
	return lerp(1.0, visibility, ShadowStrength);
}

// Number of AO rays per pixel per frame. Compile-time (changes the shader's cost class, not a live knob) —
// kept low because per-frame sample rotation + TAA accumulate many effective samples over time.
#define AO_RAY_COUNT 2

// Number of GI hemisphere rays per pixel per frame (#118). Compile-time cost-class knob, like AO. A full
// hemisphere integral is noisier than one shadow/reflection ray, so this leans hard on per-frame rotation
// + TAA to converge; kept low for cost.
#define GI_RAY_COUNT 2

// Ray-traced ambient occlusion (#118): shoot AO_RAY_COUNT cosine-weighted hemisphere rays around the
// surface normal `N`; each is a short occlusion ray (TMax = AORadius). A near hit darkens more than a far
// one (distance falloff). Returns an occlusion FACTOR in [0,1] (1 = fully open, 0 = fully occluded) already
// scaled by AOIntensity. `Ng` offsets the ray origin off the surface (acne guard, like the shadow path).
// The sample set is rotated per pixel + per frame (FrameCounter) so TAA averages successive frames into a
// smooth result — few rays here, many effective samples after temporal accumulation. RT permutation only.
float RayTraceAO(float3 positionWS, float3 Ng, float3 N, float radius, float2 pixelPos)
{
	// Build an orthonormal basis (tangent, bitangent, N) to orient the hemisphere.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	// Per-pixel + per-frame rotation seed. Interleaved-gradient-noise style hash of the pixel position,
	// offset by the frame counter so each frame draws a different rotation → TAA converges the noise out.
	const float2 px = pixelPos + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02;

	float occlusion = 0.0;
	[unroll] for (int s = 0; s < AO_RAY_COUNT; ++s)
	{
		// Cosine-weighted hemisphere sample. Stratify by ray index, jitter by the per-pixel/frame hash.
		const float u1 = frac((float(s) + ign) / float(AO_RAY_COUNT));
		const float u2 = frac(ign + float(s) * 0.61803398875); // golden-ratio decorrelation
		const float r = sqrt(u1);
		const float phi = 2.0 * PI * u2;
		const float3 localDir = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
		const float3 dir = normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * N);

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = dir;
		ray.TMin = 0.0;
		ray.TMax = radius;

		RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		q.Proceed();

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// Distance falloff: a hit right at the surface fully occludes; one near AORadius barely does.
			occlusion += 1.0 - saturate(q.CommittedRayT() / radius);
		}
	}

	occlusion = (occlusion / float(AO_RAY_COUNT)) * AOIntensity;
	return saturate(1.0 - occlusion);
}

// Reassemble the reflection geometry table's device address from the two FrameCB halves (see
// RendererService FrameCB: split lo/hi to keep the cbuffer 4-byte-scalar). 0 = no table this frame.
uint64_t ReflGeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

// One reflection geometry record, matching GeometryRecord (ReflectionGeometrySingleton.hpp) byte-for-byte.
// Read field-by-field via vk::RawBufferLoad off the record's base address (dx layout, 112-byte stride).
struct GeoRecord
{
	uint64_t VertexAddress;
	uint64_t IndexAddress;
	uint AlbedoTextureIndex;
	float4 BaseColor;
	float4x4 Model;
};

GeoRecord LoadGeoRecord(uint64_t tableAddr, uint instanceIndex)
{
	const uint64_t base = tableAddr + uint64_t(instanceIndex) * 112ull;
	GeoRecord r;
	r.VertexAddress = vk::RawBufferLoad<uint64_t>(base + 0, 8);
	r.IndexAddress = vk::RawBufferLoad<uint64_t>(base + 8, 8);
	r.AlbedoTextureIndex = vk::RawBufferLoad<uint>(base + 16, 4);
	r.BaseColor = float4(vk::RawBufferLoad<float>(base + 32, 4), vk::RawBufferLoad<float>(base + 36, 4),
	                     vk::RawBufferLoad<float>(base + 40, 4), vk::RawBufferLoad<float>(base + 44, 4));
	// mat4 is 16 contiguous floats at offset 48 (column-major, matching glm).
	float4 c0 = float4(vk::RawBufferLoad<float>(base + 48, 4), vk::RawBufferLoad<float>(base + 52, 4),
	                   vk::RawBufferLoad<float>(base + 56, 4), vk::RawBufferLoad<float>(base + 60, 4));
	float4 c1 = float4(vk::RawBufferLoad<float>(base + 64, 4), vk::RawBufferLoad<float>(base + 68, 4),
	                   vk::RawBufferLoad<float>(base + 72, 4), vk::RawBufferLoad<float>(base + 76, 4));
	float4 c2 = float4(vk::RawBufferLoad<float>(base + 80, 4), vk::RawBufferLoad<float>(base + 84, 4),
	                   vk::RawBufferLoad<float>(base + 88, 4), vk::RawBufferLoad<float>(base + 92, 4));
	float4 c3 = float4(vk::RawBufferLoad<float>(base + 96, 4), vk::RawBufferLoad<float>(base + 100, 4),
	                   vk::RawBufferLoad<float>(base + 104, 4), vk::RawBufferLoad<float>(base + 108, 4));
	r.Model = float4x4(c0, c1, c2, c3);
	return r;
}

// Read a mesh vertex's TexCoord (float2 @ offset 24 in the 48-byte Vertex) by device address.
float2 LoadVertexUV(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 24ull;
	return float2(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4));
}

// Read a mesh vertex's object-space Normal (float3 @ offset 12) by device address.
float3 LoadVertexNormal(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 12ull;
	return float3(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4));
}

// Ray-traced reflection (#118): trace the reflection ray `R` from the surface, find the closest hit,
// resolve it to a surface via the per-instance geometry table (device-address vertex/index buffers +
// material), and return its color. `lit`=false returns the raw reflected ALBEDO (the debug view — proves
// hit resolution); `lit`=true RE-LIGHTS the hit cheaply (albedo * (sun*shadow-ray + IBL-ambient)) so the
// reflection matches the scene's lighting. On a miss (or no table), return the prefiltered sky cube in `R`
// — exactly the env source the IBL specular uses. One bounce only; the reflected hit is NOT itself
// reflective (no recursion). `Ng` offsets the ray origin off the surface.
// Resolve a committed inline-RayQuery triangle hit to its surface albedo via the bindless geometry table
// (device-address vertex/index reads + barycentric UV + bindless albedo sample). Just the albedo — used by
// the Reflections debug view. Caller guarantees tableAddr != 0. `hitPos` (out) is the world hit position,
// recovered by the caller from the ray; here we return albedo + the interpolated world normal for the
// lit path to reuse (avoids a second RawBufferLoad of the same record).
struct HitSurface
{
	float3 Albedo;
	float3 Nw; // interpolated world normal
};
HitSurface ResolveHit(uint64_t tableAddr, uint instanceId, uint prim, float2 bary)
{
	const GeoRecord rec = LoadGeoRecord(tableAddr, instanceId);

	const uint64_t idxBase = rec.IndexAddress + uint64_t(prim) * 12ull; // 3 * uint32
	const uint i0 = vk::RawBufferLoad<uint>(idxBase + 0, 4);
	const uint i1 = vk::RawBufferLoad<uint>(idxBase + 4, 4);
	const uint i2 = vk::RawBufferLoad<uint>(idxBase + 8, 4);

	const float w = 1.0 - bary.x - bary.y;
	const float2 uv = w * LoadVertexUV(rec.VertexAddress, i0) + bary.x * LoadVertexUV(rec.VertexAddress, i1) + bary.y * LoadVertexUV(rec.VertexAddress, i2);

	HitSurface s;
	s.Albedo = rec.BaseColor.rgb;
	if (rec.AlbedoTextureIndex != 0)
	{
		s.Albedo *= Textures[NonUniformResourceIndex(rec.AlbedoTextureIndex)].SampleLevel(LinearSampler, uv, 0).rgb;
	}
	// Interpolated object normal -> world via the record's Model (rows hold glm's columns, so
	// mul(n, Model3x3) computes glmModel * n). Ignores non-uniform scale (inverse-transpose) — fine here.
	const float3 nObj = w * LoadVertexNormal(rec.VertexAddress, i0) + bary.x * LoadVertexNormal(rec.VertexAddress, i1) + bary.y * LoadVertexNormal(rec.VertexAddress, i2);
	s.Nw = normalize(mul(nObj, (float3x3)rec.Model));
	return s;
}

// Shade a committed hit as LIT surface radiance (#118): resolve it (ResolveHit) then re-light cheaply —
// sun (DirectionalLights[0]) with a shadow ray from the hit + an IBL/flat ambient fill (so a hit on a
// shadowed surface still contributes its ambient, not black). ONE bounce: the shaded hit does NOT itself
// trace reflections/GI (its ambient is the cheap IBL term). Shared by RT reflections and RTGI so both get
// the same correct hit shading. `hitPos` = world hit position (caller: rayOrigin + rayDir * CommittedRayT).
float3 ShadeSurfaceHit(uint64_t tableAddr, uint instanceId, uint prim, float2 bary, float3 hitPos)
{
	const HitSurface s = ResolveHit(tableAddr, instanceId, prim, bary);

	float3 direct = float3(0, 0, 0);
	if (LightCount > 0)
	{
		const float3 Lsun = normalize(-DirectionalLights[0].Direction);
		const float ndl = saturate(dot(s.Nw, Lsun));
		if (ndl > 0.0)
		{
			const float sh = RayTraceShadow(hitPos, s.Nw, Lsun, 1e30);
			direct = DirectionalLights[0].Color * DirectionalLights[0].Intensity * ndl * sh;
		}
	}

	float3 ambient;
	if (IrradianceCubeIndex != 0)
	{
		ambient = Cubemaps[NonUniformResourceIndex(IrradianceCubeIndex)].SampleLevel(LinearSampler, s.Nw, 0).rgb * IBLIntensity;
	}
	else
	{
		ambient = float3(0.03, 0.03, 0.03); // faint fill so shadowed/indirect areas aren't crushed to black
	}

	return s.Albedo * (direct + ambient);
}

// Ray-traced reflection (#118): trace R (glossy-jittered by roughness), resolve + re-light the hit
// (ShadeSurfaceHit) or reflect the sky on a miss. lit=false returns raw resolved albedo (debug view).
float3 RayTraceReflection(float3 positionWS, float3 Ng, float3 R, bool lit, float roughness, float2 pixelPos)
{
	const uint64_t tableAddr = ReflGeoTableAddress();

	// Glossy cone jitter (#118 Inc 4): a rough surface reflects a BLURRY image, not a sharp mirror. Perturb
	// the reflection direction within a disk of radius (roughness * ReflConeScale) in the plane
	// perpendicular to R, using the SAME frame-rotated IGN hash + disk sample + orthonormal basis as the
	// soft-shadow / RTAO traces. One ray/frame; per-frame rotation + TAA accumulate a smooth glossy lobe
	// over time. roughness == 0 (a mirror) => zero cone => the exact sharp ray, so mirrors are unchanged.
	float3 dir = R;
	if (roughness > 0.0)
	{
		const float3 up = abs(R.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
		const float3 tangent = normalize(cross(up, R));
		const float3 bitangent = cross(R, tangent);
		const float2 px = pixelPos + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
		const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
		const float coneRadius = roughness * ReflConeScale;
		const float rr = coneRadius * sqrt(ign);
		const float phi = 2.0 * PI * frac(ign + 0.61803398875); // golden-ratio decorrelation of angle vs radius
		dir = normalize(R + (rr * cos(phi)) * tangent + (rr * sin(phi)) * bitangent);
	}

	RayDesc ray;
	ray.Origin = positionWS + Ng * 0.02 + dir * 0.01; // normal-offset to dodge self-hit
	ray.Direction = dir;
	ray.TMin = 0.0;
	// Bounded by ReflRange (#118 perf): a ray finding nothing within this distance falls back to the sky
	// cube below, so capping TMax lets the BVH traversal early-out instead of walking the whole scene extent
	// on every sky-bound ray. No visual change within range; past it, distant geometry reflects as sky.
	ray.TMax = ReflRange;

	// Closest hit (no ACCEPT_FIRST_HIT): a reflection needs the FRONT-MOST surface along the ray.
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();

	if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || tableAddr == 0)
	{
		// Miss (or no geometry table): reflect the distant sky along the (jittered) direction.
		if (PrefilteredCubeIndex != 0)
		{
			return Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb;
		}
		return float3(0, 0, 0);
	}

	if (!lit)
	{
		return ResolveHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics()).Albedo; // debug view
	}

	const float3 hitPos = ray.Origin + dir * q.CommittedRayT();
	return ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos);
}

// Ray-traced 1-bounce diffuse global illumination (#118). From the shaded point, trace GI_RAY_COUNT
// cosine-weighted hemisphere rays around N (the SAME ray-gen as RTAO), shade each committed hit as lit
// surface radiance (ShadeSurfaceHit — albedo * sun-with-shadow-ray + ambient), and average. On a miss the
// ray sees open sky, contributing the sky/IBL radiance in that direction (so sky bounce isn't lost). The
// cosine weight is baked into the sampling, so the Monte-Carlo estimate of incoming radiance is just the
// mean of the samples; multiply by the receiver albedo (diffuse response) here. Bounded by GIRange so far
// geometry doesn't dominate and to cap cost. Per-frame IGN rotation + TAA converge the few rays. One
// bounce: the shaded hits use the cheap IBL/flat ambient, no recursion. RT permutation only.
float3 RayTraceGI(float3 positionWS, float3 Ng, float3 N, float3 receiverAlbedo, float2 pixelPos)
{
	const uint64_t tableAddr = ReflGeoTableAddress();

	// Orthonormal basis (tangent, bitangent, N) to orient the hemisphere — same as RTAO.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	const float2 px = pixelPos + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02;

	float3 incoming = float3(0, 0, 0);
	[unroll] for (int s = 0; s < GI_RAY_COUNT; ++s)
	{
		// Cosine-weighted hemisphere sample (stratified by ray index, jittered by the hash) — identical to RTAO.
		const float u1 = frac((float(s) + ign) / float(GI_RAY_COUNT));
		const float u2 = frac(ign + float(s) * 0.61803398875);
		const float r = sqrt(u1);
		const float phi = 2.0 * PI * u2;
		const float3 localDir = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
		const float3 dir = normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * N);

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = dir;
		ray.TMin = 0.0;
		ray.TMax = GIRange;

		RayQuery<RAY_FLAG_CULL_NON_OPAQUE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		q.Proceed();

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT && tableAddr != 0)
		{
			const float3 hitPos = origin + dir * q.CommittedRayT();
			incoming += ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos);
		}
		else if (PrefilteredCubeIndex != 0)
		{
			// Miss -> open sky along the ray: the prefiltered env contributes sky bounce (dialed by IBLIntensity
			// so it's consistent with the baked ambient this GI adds on top of).
			incoming += Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb * IBLIntensity;
		}
	}

	return (incoming / float(GI_RAY_COUNT)) * receiverAlbedo * GIIntensity;
}
#endif

// Directional-sun shadow: RT ray query (when RTShadowEnabled) or the raster shadow map (dedicated map,
// gated by ShadowMapIndex; 0 = no shadows). `Ng`/`L`/`pixelPos` are only used by the RT path.
float SampleSunShadow(float3 positionWS, float3 Ng, float3 L, float NdotL, float2 pixelPos)
{
#ifdef SS_RAYTRACING
	if (RTShadowEnabled != 0)
	{
		// Soft (cone-sampled penumbra) when enabled — the sun's angular half-size subtends a disk of
		// radius tan(SunAngularRadius) perpendicular to L. Else the hard single ray. Sun is at infinity.
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, 1e30, tan(SunAngularRadius), pixelPos);
		}
		return RayTraceShadow(positionWS, Ng, L, 1e30);
	}
#endif
	if (ShadowMapIndex == 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(ShadowMapIndex, LightViewProj, float4(0, 0, 1, 1), positionWS, NdotL);
}

// Spot shadow: RT ray query (when RTShadowEnabled and this spot casts) or the shared raster atlas at the
// spot's tile. `Ng` = geometric normal (ray offset), `L` = direction to the light, `distToLight` = ray
// length for the RT path. Raster path gated by the atlas index being bound AND the spot having a tile
// (ShadowIndex >= 0); RT path gated by ShadowIndex >= 0 alone (the "this light casts" sentinel).
float SampleSpotShadow(SpotLight spot, float3 positionWS, float3 Ng, float3 L, float distToLight, float NdotL, float2 pixelPos)
{
#ifdef SS_RAYTRACING
	if (RTShadowEnabled != 0)
	{
		if (spot.ShadowIndex < 0)
		{
			return 1.0; // this spot doesn't cast
		}
		// Stop just short of the light so the ray can't hit geometry at/behind the light position.
		const float tMax = max(distToLight - 0.05, 0.0);
		// Soft: a source of radius LightSourceRadius at distToLight subtends a cone of half-angle whose
		// tangent is (radius / distance) — bigger/closer source => wider penumbra.
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, tMax, LightSourceRadius / max(distToLight, 1e-4), pixelPos);
		}
		return RayTraceShadow(positionWS, Ng, L, tMax);
	}
#endif
	if (SpotShadowAtlasIndex == 0 || spot.ShadowIndex < 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(SpotShadowAtlasIndex, spot.ShadowViewProj, spot.ShadowAtlasRect, positionWS, NdotL);
}

// Pick which of a point light's 6 cube faces a world-space direction belongs to. Faces are indexed
// +X,-X,+Y,-Y,+Z,-Z (matching ShadowPass::ComputePointFaceViewProj): the dominant (largest magnitude)
// component chooses the axis, its sign chooses the face. Because we sample with the same matrix we
// rendered the face with, this stays self-consistent (no cube sampler / orientation convention needed).
int PointShadowFace(float3 dir)
{
	const float3 a = abs(dir);
	if (a.x >= a.y && a.x >= a.z)
	{
		return dir.x >= 0.0 ? 0 : 1; // +X : -X
	}
	if (a.y >= a.z)
	{
		return dir.y >= 0.0 ? 2 : 3; // +Y : -Y
	}
	return dir.z >= 0.0 ? 4 : 5; // +Z : -Z
}

// Point (omni) shadow: RT ray query (when RTShadowEnabled and this light casts) or the raster point atlas.
// `Ng` = geometric normal (ray offset), `L` = direction to the light, `distToLight` = ray length for RT.
// Raster path picks the cube face the surface lies on and PCF-samples that face's tile, gated by the atlas
// being bound AND a shadow slot assigned (ShadowSlot >= 0); RT path gated by ShadowSlot >= 0 alone.
float SamplePointShadow(PointLight light, float3 positionWS, float3 Ng, float3 L, float distToLight, float NdotL, float2 pixelPos)
{
#ifdef SS_RAYTRACING
	if (RTShadowEnabled != 0)
	{
		if (light.ShadowSlot < 0)
		{
			return 1.0; // this light doesn't cast
		}
		const float tMax = max(distToLight - 0.05, 0.0);
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, tMax, LightSourceRadius / max(distToLight, 1e-4), pixelPos);
		}
		return RayTraceShadow(positionWS, Ng, L, tMax);
	}
#endif
	if (PointShadowAtlasIndex == 0 || light.ShadowSlot < 0)
	{
		return 1.0;
	}
	const int face = PointShadowFace(positionWS - light.Position);
	const PointShadow payload = PointShadows[light.ShadowSlot];
	return SampleShadowFactor(PointShadowAtlasIndex, payload.Face[face], payload.Rect[face], positionWS, NdotL);
}

// Tonemap + sRGB encode moved to the post-process pass (Tonemap.frag.hlsl, #53). This shader outputs
// raw linear HDR into the scene target that the post pass then tonemaps.

// --- Cook-Torrance terms ---
float DistributionGGX(float3 N, float3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	// Direct-lighting remap of roughness (Disney/UE4).
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Roughness-aware Fresnel for the ambient/IBL term (Sebastien Lagarde): rough surfaces shouldn't show
// a full grazing Fresnel boost the way a smooth one does.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	const float3 fMax = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
	return F0 + (fMax - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Split-sum image-based lighting: diffuse from the irradiance cube, specular from the prefiltered cube
// (roughness -> mip) modulated by the BRDF LUT. Returns 0 (caller falls back to analytic ambient) when
// IBL isn't baked (IrradianceCubeIndex == 0).
// positionWS/Ng/pixelPos are only used by the RT reflection blend (SS_RAYTRACING); the raster build
// ignores them. When useGIDiffuse != 0, the baked-irradiance DIFFUSE term is REPLACED by giDiffuse (the
// traced 1-bounce GI) — the diffuse indirect becomes scene-derived instead of the constant sky
// approximation (Lumen/RTXGI model). Specular (env cube + RT reflection) is unaffected. giDiffuse already
// carries the receiver albedo + GIIntensity; kd (metal energy) and ao still modulate it here.
float3 ComputeIBL(float3 N, float3 V, float3 albedo, float3 F0, float roughness, float metallic, float ao, float3 positionWS, float3 Ng, float2 pixelPos, uint useGIDiffuse, float3 giDiffuse)
{
	if (IrradianceCubeIndex == 0)
	{
		return float3(0, 0, 0);
	}

	const float NdotV = max(dot(N, V), 0.0);
	const float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	const float3 kd = (1.0 - F) * (1.0 - metallic); // metals have no diffuse

	// Diffuse indirect: normally the baked irradiance cube * albedo (a constant sky approximation). When RT
	// GI is active, REPLACE it with the traced 1-bounce indirect (giDiffuse already carries albedo +
	// GIIntensity) so the diffuse fill is scene-derived (color bleed, contact fill) instead of the guess.
	// The IBLIntensity dial applies only to the baked path; giDiffuse has its own GIIntensity.
	float3 diffuse;
	if (useGIDiffuse != 0)
	{
		diffuse = giDiffuse; // scene-traced; NOT scaled by IBLIntensity (see return)
	}
	else
	{
		const float3 irradiance = Cubemaps[NonUniformResourceIndex(IrradianceCubeIndex)].SampleLevel(LinearSampler, N, 0).rgb;
		diffuse = irradiance * albedo * IBLIntensity;
	}

	// Specular env radiance: the prefiltered cube at the reflection vector (roughness -> mip).
	const float3 R = reflect(-V, N);
	const float lod = roughness * float(max(PrefilteredMipCount, 1u) - 1u);
	const float3 envRadiance = Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, R, lod).rgb;

	// BRDF LUT (split-sum scale+bias), indexed by (NdotV, roughness). Sampled through ClampSampler (NOT the
	// wrapping LinearSampler): a LUT must clamp, or a bilinear tap at NdotV~1 wraps to the grazing edge and
	// produces a hard brightness seam down the middle of the view. This split-sum weight applies to BOTH the
	// env-cube and the RT reflection, so their energy/Fresnel stay consistent.
	const float2 brdf = Textures[NonUniformResourceIndex(BRDFLutIndex)].SampleLevel(ClampSampler, float2(NdotV, roughness), 0).rg;
	const float3 specWeight = F0 * brdf.x + brdf.y;

	// Env-cube specular is part of the baked ambient approximation, so it's dialed by IBLIntensity like the
	// diffuse below.
	float3 specular = envRadiance * specWeight * IBLIntensity;

	// RT reflections (#118): for smooth surfaces, blend in a traced, re-lit reflection of the ACTUAL scene.
	// reflWeight is a PURE roughness falloff (rough surfaces stay on the cheap cube; ReflMaxRoughness is the
	// cutoff) — it decides how mirror-like the surface is, NOT how bright. Brightness is the RT term's OWN
	// ReflIntensity dial, deliberately DECOUPLED from IBLIntensity: an RT reflection is real scene light, not
	// the baked-ambient approximation, so turning ambient down must not dim it.
#ifdef SS_RAYTRACING
	if (RTReflEnabled != 0 && roughness < ReflMaxRoughness)
	{
		const float reflWeight = saturate(1.0 - roughness / max(ReflMaxRoughness, 1e-3));
		const float3 rt = RayTraceReflection(positionWS, Ng, R, true, roughness, pixelPos); // lit, glossy-jittered
		const float3 specularRT = rt * specWeight * ReflIntensity;
		specular = lerp(specular, specularRT, reflWeight);
	}
#endif

	// diffuse already carries its own scale (IBLIntensity for the baked path, GIIntensity for the traced GI);
	// specular likewise (IBLIntensity for the env cube, ReflIntensity for the RT reflection). kd (metal
	// energy) + ao modulate the whole ambient.
	return (kd * diffuse + specular) * ao;
}

// One light's Cook-Torrance contribution (diffuse + specular), given the already-normalized light
// direction L and the incoming radiance (color * intensity, pre-attenuation). Shared by the
// directional, point, and spot loops -- only how L/radiance are computed differs per light type.
float3 ShadePBR(float3 N, float3 V, float3 L, float3 F0, float3 albedo, float metallic, float roughness, float3 radiance)
{
	const float3 H = normalize(V + L);
	const float NdotL = max(dot(N, L), 0.0);

	const float D = DistributionGGX(N, H, roughness);
	const float G = GeometrySmith(N, V, L, roughness);
	const float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

	const float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4);
	const float3 kd = (1.0 - F) * (1.0 - metallic); // metals have no diffuse

	return (kd * albedo / PI + specular) * radiance * NdotL;
}

// World-space shading normal: perturb the geometric normal by the tangent-space normal map when one
// is bound, otherwise fall back to the interpolated vertex normal.
float3 ResolveNormal(PSInput i, uint normalIndex)
{
	float3 N = normalize(i.NormalWS);
	if (normalIndex == 0)
	{
		return N;
	}
	float3 T = normalize(i.TangentWS.xyz);
	// Re-orthogonalize (Gram-Schmidt) so interpolation skew doesn't tilt the basis.
	T = normalize(T - N * dot(N, T));
	float3 B = cross(N, T) * i.TangentWS.w;                                   // handedness sign baked at import
	float3 sampled = SampleBindless(normalIndex, i.TexCoord).xyz * 2.0 - 1.0; // [0,1] -> [-1,1]
	float3x3 TBN = float3x3(T, B, N);
	return normalize(mul(sampled, TBN));
}

float4 main(PSInput i) : SV_Target0
{
	// Per-instance albedo override: a non-zero per-instance index wins over the material default,
	// so objects sharing one material can each show a different texture (and still batch).
	const uint instAlbedo = Instances[i.InstanceID].AlbedoTextureIndex;
	const uint albedoIndex = (instAlbedo != 0) ? instAlbedo : AlbedoTextureIndex;
	const float4 albedoSample = (albedoIndex != 0) ? SampleBindless(albedoIndex, i.TexCoord) : float4(1, 1, 1, 1);

	// Alpha-cutout (glTF MASK): discard texels whose alpha (texture * BaseColor.a) is below the cutoff,
	// BEFORE any lighting so masked-out fragments cost nothing and don't write depth. clip() discards when
	// its argument is < 0. Opaque-pass masking only — no blending/sorting. Off (AlphaMaskEnabled == 0) for
	// normal materials, so this is a no-op there.
	if (AlphaMaskEnabled != 0)
	{
		clip(albedoSample.a * BaseColor.a - AlphaCutoff);
	}

	const float3 albedo = albedoSample.rgb * BaseColor.rgb;

	// Metallic-roughness from the packed MR texture (glTF: G = roughness, B = metallic) * factors.
	float roughness = Roughness;
	float metallic = Metallic;
	if (MetallicRoughnessTextureIndex != 0)
	{
		float3 mr = SampleBindless(MetallicRoughnessTextureIndex, i.TexCoord).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
	roughness = clamp(roughness, 0.04, 1.0); // avoid a zero-area specular lobe

	float ao = (AOTextureIndex != 0) ? SampleBindless(AOTextureIndex, i.TexCoord).r : 1.0;

	const float3 N = ResolveNormal(i, NormalTextureIndex);
	const float3 V = normalize(CameraPosition - i.PositionWS);
	const float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

	// Ray-traced ambient occlusion (#118): fold the RTAO factor into `ao` so BOTH the IBL branch and the
	// analytic-hemisphere fallback below inherit it (each multiplies by `ao`). Multiplies the material AO
	// map, never brightens. Offset the ray origin along the geometric (interpolated vertex) normal, and
	// orient the hemisphere by the mapped shading normal N. Direct lighting (Lo) is untouched.
#ifdef SS_RAYTRACING
	if (RTAOEnabled != 0)
	{
		ao *= RayTraceAO(i.PositionWS, normalize(i.NormalWS), N, AORadius, i.PositionCS.xy);
	}
#endif

	// Debug view (#118): output the isolated grayscale AO term (material AO * RTAO) so the RTAO radius/
	// intensity can be tuned against the raw signal without the lit scene on top. Returns pre-tonemap
	// linear; the tonemap pass leaves it near-grayscale (AO is [0,1], so exposure/ACES barely shift it).
	if (DebugAO != 0)
	{
		return float4(ao, ao, ao, 1.0);
	}

	// Debug view (#118): output the raw reflected albedo (RT reflection hit resolution) so the geometry-table
	// resolve — device-address vertex/index reads + barycentric UV + bindless albedo sample — is verifiable
	// on screen independent of the lighting blend. Only meaningful in the RT permutation.
#ifdef SS_RAYTRACING
	if (DebugReflections != 0)
	{
		const float3 R = reflect(-V, N);
		const float3 refl = RayTraceReflection(i.PositionWS, normalize(i.NormalWS), R, false, roughness, i.PositionCS.xy); // raw albedo
		return float4(refl, 1.0);
	}
#endif

	// 1-bounce RT diffuse GI (#118): gather indirect light once here so both the debug view and the additive
	// ambient below reuse it. Computed when GI is enabled OR the GI debug view is active (so the debug view
	// works even before enabling the additive fold). Compiled out on non-RT devices.
	float3 giIndirect = float3(0, 0, 0);
#ifdef SS_RAYTRACING
	if (RTGIEnabled != 0 || DebugGI != 0)
	{
		giIndirect = RayTraceGI(i.PositionWS, normalize(i.NormalWS), N, albedo, i.PositionCS.xy);
	}
	// Debug view 4 = GI: output the raw indirect term for tuning (intensity/range) against the raw signal.
	if (DebugGI != 0)
	{
		return float4(giIndirect, 1.0);
	}
#endif

	float3 Lo = float3(0, 0, 0);

	// --- Directional lights (the sun). Only light 0 casts shadows in this single-map implementation.
	const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
	[loop] for (int l = 0; l < count; ++l)
	{
		const float3 L = normalize(-DirectionalLights[l].Direction);
		const float3 radiance = DirectionalLights[l].Color * DirectionalLights[l].Intensity;
		// Shadow multiplies the whole contribution; ambient is unaffected so shadows stay lit-but-dim. N is
		// the shading normal, reused to offset the RT shadow ray origin off the surface (good enough here).
		const float shadow = (l == 0) ? SampleSunShadow(i.PositionWS, N, L, max(dot(N, L), 0.0), i.PositionCS.xy) : 1.0;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * shadow;
	}

	// --- Point lights: inverse-square falloff with a smooth windowed cutoff at Range (UE4/Frostbite).
	// The window ((1-(d/R)^4)^2) drives the contribution to exactly zero at d==Range instead of an abrupt
	// clip, so there's no hard lit/unlit edge. Shadow (omni cube atlas) multiplies the whole contribution.
	const int pointCount = clamp(PointCount, 0, MAX_POINT_LIGHTS);
	[loop] for (int p = 0; p < pointCount; ++p)
	{
		const float3 toLight = PointLights[p].Position - i.PositionWS;
		const float dist = length(toLight);
		const float3 L = toLight / max(dist, 1e-4);

		const float range = max(PointLights[p].Range, 1e-4);
		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);

		// Shadow: 1 when unshadowed / this light casts no shadow. NdotL uses the surface normal vs L. The RT
		// path traces from the surface to the light (dist), offset by N.
		const float pointShadow = SamplePointShadow(PointLights[p], i.PositionWS, N, L, dist, max(dot(N, L), 0.0), i.PositionCS.xy);

		const float3 radiance = PointLights[p].Color * PointLights[p].Intensity * atten;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * pointShadow;
	}

	// --- Spot lights: point attenuation multiplied by a smooth cone falloff between the inner/outer
	// half-angles (stored as cosines). -L is the light->surface direction compared to the spot's
	// forward axis. Unshadowed.
	const int spotCount = clamp(SpotCount, 0, MAX_SPOT_LIGHTS);
	[loop] for (int s = 0; s < spotCount; ++s)
	{
		const float3 toLight = SpotLights[s].Position - i.PositionWS;
		const float dist = length(toLight);
		const float3 L = toLight / max(dist, 1e-4);

		const float range = max(SpotLights[s].Range, 1e-4);
		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);

		// Cone falloff: 1 inside the inner angle, smoothly to 0 at the outer angle. cos() decreases with
		// angle, so a larger dot() == closer to the axis == more lit.
		const float cosAngle = dot(-L, SpotLights[s].Direction);
		const float denom = max(SpotLights[s].CosInner - SpotLights[s].CosOuter, 1e-4);
		const float cone = pow(saturate((cosAngle - SpotLights[s].CosOuter) / denom), 2.0);

		// Shadow: 1 when unshadowed / this spot casts no shadow. NdotL uses the surface normal vs L. The RT
		// path traces from the surface to the spot (dist), offset by N.
		const float spotShadow = SampleSpotShadow(SpotLights[s], i.PositionWS, N, L, dist, max(dot(N, L), 0.0), i.PositionCS.xy);

		const float3 radiance = SpotLights[s].Color * SpotLights[s].Intensity * atten * cone;
		Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * spotShadow;
	}

	// 1-bounce RT diffuse GI (#118): when active, the traced indirect REPLACES the DIFFUSE ambient (Lumen/
	// RTXGI model) — the diffuse fill becomes scene-derived (color bleed, contact fill) instead of the
	// constant sky guess. Specular ambient (env cube / RT reflection) is unaffected. Off => the baked/
	// analytic diffuse as before.
	uint useGI = 0;
	float3 giDiffuse = float3(0, 0, 0);
#ifdef SS_RAYTRACING
	if (RTGIEnabled != 0)
	{
		useGI = 1;
		giDiffuse = giIndirect; // already albedo * GIIntensity
	}
#endif

	// Ambient: prefer split-sum IBL (baked from the sky) when available — metals reflect the environment
	// and specular picks up sky color. Falls back to the analytic hemisphere ambient (same zenith/horizon/
	// ground colors the sky shows) when IBL isn't baked, so the look degrades gracefully.
	float3 ambient = ComputeIBL(N, V, albedo, F0, roughness, metallic, ao, i.PositionWS, normalize(i.NormalWS), i.PositionCS.xy, useGI, giDiffuse);
	if (IrradianceCubeIndex == 0)
	{
		// No baked IBL: analytic hemisphere diffuse, OR the traced GI diffuse when GI replaces it.
		float3 ambientDiffuse;
		if (useGI != 0)
		{
			ambientDiffuse = giDiffuse; // scene-traced diffuse (carries albedo + GIIntensity)
		}
		else
		{
			const float3 ambientEnv = (N.y >= 0.0)
			                              ? lerp(SkyHorizonColor, SkyZenithColor, saturate(N.y))
			                              : lerp(SkyHorizonColor, GroundColor, saturate(-N.y));
			ambientDiffuse = ambientEnv * SkyIntensity * albedo;
		}
		ambient = ambientDiffuse * ao;
	}

	float3 color = Lo + ambient;

	// Emissive (sRGB-sampled) + scalar factor.
	if (EmissiveTextureIndex != 0)
	{
		color += SampleBindless(EmissiveTextureIndex, i.TexCoord).rgb * EmissiveColor;
	}
	else
	{
		color += EmissiveColor;
	}

	// Output raw LINEAR HDR radiance into the HDR scene target. The exposure/ACES/sRGB output transform
	// now lives once in the post-process pass (Tonemap.frag.hlsl, #53), which samples this target — the
	// mesh and sky shaders no longer tonemap inline.
	return float4(color, BaseColor.a);
}