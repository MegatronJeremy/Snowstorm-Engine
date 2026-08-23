// RTHitShading.hlsli - the shared inline-RayQuery hit resolution + one-bounce shading for the COMPUTE
// RT passes (GI #124, Reflection #129). Both trace the bindless SceneTLAS, resolve a committed triangle hit
// to its surface via the per-instance geometry table (device-address vertex/index reads + barycentric UV
// + bindless albedo), and re-light it cheaply: sun with a shadow ray, the surface's own emissive, an IBL/flat
// ambient fill, and (under RTHIT_LOCAL_LIGHTS) ONE stochastically-chosen local light with a shadow ray of
// its own. Extracted from the TEMPORARY copy that lived in GI.comp.hlsl (the #124 note foreshadowed this)
// so the two compute passes share ONE implementation instead of drifting copies.
//
// NOTE: this is the COMPUTE flavour: the sun comes from the includer's OWN constant buffer as scalar
// fields (SunDirection/SunColor/SunIntensity), NOT the DirectionalLights[] material-set array a fragment
// shader reads. That is why DefaultLit.frag does not include this file.
//
// CONTRACT - the includer MUST declare, BEFORE #include-ing this file:
//   * set 3 bindless is declared HERE (Textures/Cubemaps/SceneTLAS): identical in every compute RT pass,
//     gap-filled by the compute pipeline builder. Do NOT re-declare it in the includer.
//   * a clamp/wrap sampler named `LinearSampler` (the includer owns it on set 0).
//   * these constant-buffer scalars (any cbuffer or binding, since they are referenced by name):
//       uint  LightCount;          // 0 = no sun
//       float3 SunDirection;       // world-space light direction (points FROM the light)
//       float3 SunColor;  float SunIntensity;
//       float ShadowStrength;      // lerp(1, visibility, ShadowStrength)
//       uint  IrradianceCubeIndex; // bindless cube for hit ambient (0 = flat 0.03 fill)
//       float IBLIntensity;
//   * OPTIONAL local (point/spot) lighting at the hit: #define RTHIT_LOCAL_LIGHTS before the include and
//     add these five to the same cbuffer. Without the define the block is compiled out entirely, which is
//     what keeps its two extra RayQuery traversals out of an includer that does not want them.
//       uint  FrameCounter;        // decorrelates the stochastic light pick across frames
//       uint  HitLightCount;       // 0 = sun only (also the render.rt.hit_lights off state)
//       float4 HitLightPosRange[16];  // xyz = position, w = range (0 = unbounded)
//       float4 HitLightColor[16];     // xyz = color * intensity, w = cos(inner) for spots
//       float4 HitLightDirCos[16];    // xyz = spot direction, w = cos(outer) (-2 = point)
//   * the reflection geometry-table address, however the includer names it, passed into these functions
//     as `tableAddr` (callers already reassemble it from their CB's lo/hi halves).

#ifndef SNOWSTORM_RT_HIT_SHADING_HLSLI
#define SNOWSTORM_RT_HIT_SHADING_HLSLI

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

// The geometry-table record + attribute reads + the any-hit alpha test now live in RTGeometry.hlsli (shared
// with the shadow/AO passes, which don't want the sun/IBL shading below). Textures[] is declared just above,
// satisfying RTGeometry's contract, so this include must follow it.
#include "RTGeometry.hlsli"

// Own constant, not the includer's PI: Reflection.comp declares its PI *after* this include.
static const float kRTHitInvPi = 0.31830988618;

float RTHitLuminance(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

struct HitSurface
{
	float3 Albedo;
	float3 Nw;       // interpolated world normal
	float Metallic;  // diffuse response is scaled by (1 - Metallic); this pass models no specular lobe
	float3 Emissive; // self-emitted radiance, added un-modulated (no albedo, no 1/PI)
};

// Resolve a committed inline-RayQuery triangle hit to its surface albedo + interpolated world normal via
// the bindless geometry table. Caller guarantees tableAddr != 0.
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
	// Factor times map, the glTF packing PathTrace.comp resolves identically (metallic in .b). The map read is
	// not optional: Sponza authors metallic=1.0 on 24 of 26 materials and carries the real value in the texture,
	// so trusting the factor alone would turn the whole scene metallic and collapse the bounce to black.
	s.Metallic = rec.Metallic;
	if (rec.MetallicRoughnessTextureIndex != 0)
	{
		s.Metallic *= Textures[NonUniformResourceIndex(rec.MetallicRoughnessTextureIndex)].SampleLevel(LinearSampler, uv, 0).b;
	}
	s.Metallic = saturate(s.Metallic);
	// Factor times map, same glTF resolution PathTrace.comp does. An emissive secondary hit is the only way a
	// self-lit surface reaches the indirect estimate: the gather never runs NEE toward emitters, so without this
	// an emissive mesh lights the frame it is visible in and contributes nothing to bounce.
	s.Emissive = rec.Emissive;
	if (rec.EmissiveTextureIndex != 0)
	{
		s.Emissive *= Textures[NonUniformResourceIndex(rec.EmissiveTextureIndex)].SampleLevel(LinearSampler, uv, 0).rgb;
	}
	// Interpolated object normal -> world via the record's Model (rows hold glm's columns, so
	// mul(n, Model3x3) computes glmModel * n). Ignores non-uniform scale (inverse-transpose), fine here.
	const float3 nObj = w * LoadVertexNormal(rec.VertexAddress, i0) + bary.x * LoadVertexNormal(rec.VertexAddress, i1) + bary.y * LoadVertexNormal(rec.VertexAddress, i2);
	s.Nw = normalize(mul(nObj, (float3x3)rec.Model));
	return s;
}

// Shadow ray for the one-bounce hit shading (ACCEPT_FIRST_HIT: occlusion only). Alpha-tests cutout occluders
// via the geometry table (masked instances are FORCE_NON_OPAQUE, so they surface as candidates here).
float RTHitShadowRay(uint64_t tableAddr, float3 positionWS, float3 Ng, float3 L, float tMax)
{
	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = L;
	ray.TMin = 0.0;
	ray.TMax = tMax;
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	while (q.Proceed())
	{
		if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
		    RTCommitCandidate(tableAddr, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), LinearSampler))
		{
			q.CommitNonOpaqueTriangleHit();
		}
	}
	const float visibility = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
	return lerp(1.0, visibility, ShadowStrength);
}

// Inverse-square with the smooth range cutoff, character-for-character PathTrace.comp's PositionalAttenuation.
// The two must agree: this bounce is measured against that path tracer as ground truth, so a different falloff
// would show up as a GI error rather than the sampling difference it actually is.
float RTHitPositionalAttenuation(float dist, float range)
{
	float atten = 1.0 / max(dist * dist, 1e-4);
	if (range > 0.0)
	{
		const float t = saturate(1.0 - pow(dist / range, 4.0));
		atten *= t * t;
	}
	return atten;
}

#ifdef RTHIT_LOCAL_LIGHTS

// Unshadowed Lambert contribution of local light `i` at (P, N), 1/PI already applied. Returns black when the
// light is out of range, below the horizon, or outside its cone; `L`/`dist` are only meaningful otherwise.
float3 RTHitLocalLightUnshadowed(uint i, float3 P, float3 N, out float3 L, out float dist)
{
	L = float3(0, 0, 1);

	const float4 posRange = HitLightPosRange[i];
	const float3 d = posRange.xyz - P;
	dist = length(d);
	const float range = posRange.w;
	if (dist < 1e-4 || (range > 0.0 && dist > range))
	{
		return float3(0, 0, 0);
	}
	L = d / dist;

	const float ndl = saturate(dot(N, L));
	if (ndl <= 0.0)
	{
		return float3(0, 0, 0);
	}

	const float4 color = HitLightColor[i];
	const float4 dirCos = HitLightDirCos[i];
	float cone = 1.0;
	if (dirCos.w > -1.5) // a real cos(outer) is in [-1, 1]; points carry the -2 sentinel the C++ packer writes
	{
		cone = smoothstep(dirCos.w, color.w, dot(normalize(dirCos.xyz), -L));
		if (cone <= 0.0)
		{
			return float3(0, 0, 0);
		}
	}

	return color.rgb * (ndl * kRTHitInvPi * cone * RTHitPositionalAttenuation(dist, range));
}

// Hash -> [0, 1). Position-seeded rather than pixel-seeded on purpose: the caller is a secondary hit, so
// neighbouring pixels can land on the SAME surface point and must not then share a light pick (that would
// correlate the noise into visible blotches the temporal filter cannot average away).
float RTHitRandom(float3 P, uint frame)
{
	const uint3 q = asuint(P);
	uint h = q.x * 73856093u ^ q.y * 19349663u ^ q.z * 83492791u ^ frame * 2654435761u;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	return float(h & 0x00FFFFFFu) / float(0x01000000u);
}

// Local (point/spot) direct lighting at a secondary hit, via UE5 MegaLights' model: importance-sample ONE
// light in proportion to its unshadowed contribution, trace ONE shadow ray, divide by the selection pdf. The
// estimator is unbiased and its cost is CONSTANT in the light count, which is the whole reason for the pick:
// full NEE at every secondary hit would multiply the ray budget by the number of lights, and the gather
// already fires render.gi.rays primary rays per pixel. Lumen instead reads a surface cache that already holds
// direct lighting at the hit; this engine has no such cache, and one shadow ray is the cheaper stand-in.
//
// This traces its own ray rather than handing the direction back to ShadeSurfaceHit to merge with the sun's
// trace. Merging was measured and is the worse trade: one shared traversal cuts ISA growth (+57% to +30% on
// GI.comp) but must keep both candidates live across it, which pushes VGPRs 85 -> 102 and drops occupancy from
// 16/16 to 13/16 waves. Occupancy is the metric that costs real throughput; ISA size is instruction cache.
float3 RTHitLocalLights(uint64_t tableAddr, float3 P, float3 N)
{
	float total = 0.0;
	for (uint i = 0; i < HitLightCount; ++i)
	{
		float3 Li;
		float di;
		total += RTHitLuminance(RTHitLocalLightUnshadowed(i, P, N, Li, di));
	}
	if (total <= 0.0)
	{
		return float3(0, 0, 0);
	}

	const float xi = RTHitRandom(P, FrameCounter) * total;
	float acc = 0.0;
	for (uint j = 0; j < HitLightCount; ++j)
	{
		float3 L;
		float dist;
		const float3 c = RTHitLocalLightUnshadowed(j, P, N, L, dist);
		const float w = RTHitLuminance(c);
		acc += w;
		if (w > 0.0 && xi < acc)
		{
			// tMax stops short of the light so the ray cannot commit geometry at or behind it. No cone
			// sampling on the source radius (the path tracer does that): a soft penumbra on a bounce is
			// second-order, and the extra sample would not survive the half-res GI denoiser anyway.
			const float vis = RTHitShadowRay(tableAddr, P, N, L, max(dist - 0.05, 0.0));
			return c * vis * (total / w); // f / pdf, pdf = w / total
		}
	}
	return float3(0, 0, 0);
}

#endif // RTHIT_LOCAL_LIGHTS

// Shade a committed hit as LIT surface radiance. Resolve it and re-light cheaply: sun (from the
// includer's CB) with a shadow ray, one importance-picked local light with a shadow ray, the surface's own
// emissive, and an IBL/flat ambient fill (so a hit on a shadowed surface still contributes its ambient, not
// black). ONE bounce: the shaded hit does NOT itself trace beyond those shadow rays. `hitPos` = world hit
// position (caller: rayOrigin + rayDir * CommittedRayT).
//
// ambientScale (#39) attenuates the un-occluded IBL ambient at THIS hit. Reflections pass 1.0 (a reflected
// surface should look fully lit). The GI gather passes render.gi.bounce_ambient (< 1): the GI is itself the
// indirect-diffuse estimator, so a full un-occluded ambient injected at every secondary hit double-counts
// the sky and floods shadowed nooks with second-hand un-occluded ambient (the residual over-brightness the
// path-traced reference exposed after #163; the PT injects no free ambient per bounce). Sun direct is
// unaffected (it carries its own shadow ray).
float3 ShadeSurfaceHit(uint64_t tableAddr, uint instanceId, uint prim, float2 bary, float3 hitPos, float ambientScale)
{
	const HitSurface s = ResolveHit(tableAddr, instanceId, prim, bary);

	float3 direct = float3(0, 0, 0);
	if (LightCount > 0)
	{
		const float3 Lsun = normalize(-SunDirection);
		const float ndl = saturate(dot(s.Nw, Lsun));
		if (ndl > 0.0)
		{
			const float sh = RTHitShadowRay(tableAddr, hitPos, s.Nw, Lsun, 1e30);
			// Lambertian BRDF normalization: SunColor*SunIntensity*ndl is IRRADIANCE, and the outgoing
			// radiance a diffuse surface emits from it is albedo/PI times that. The ambient term below
			// needs no such factor: the irradiance cube already stores E/PI (IBLIrradiance.hlsl).
			direct = SunColor * SunIntensity * ndl * sh * kRTHitInvPi;
		}
	}

#ifdef RTHIT_LOCAL_LIGHTS
	// HitLightCount is 0 whenever render.rt.hit_lights is off, so the gate costs one uniform branch.
	if (HitLightCount > 0)
	{
		direct += RTHitLocalLights(tableAddr, hitPos, s.Nw);
	}
#endif

	float3 ambient;
	if (IrradianceCubeIndex != 0)
	{
		ambient = Cubemaps[NonUniformResourceIndex(IrradianceCubeIndex)].SampleLevel(LinearSampler, s.Nw, 0).rgb * IBLIntensity;
	}
	else
	{
		ambient = float3(0.03, 0.03, 0.03); // faint fill so shadowed/indirect areas aren't crushed to black
	}

	// (1 - Metallic) on the diffuse albedo, matching DefaultLit's kd and PathTrace's diffuse lobe. A conductor
	// therefore returns black here rather than a bright diffuse reflector: this pass carries no specular lobe,
	// so under-estimating a metal bounce is the honest error, and it is the one the forward already makes.
	// Emissive is outgoing radiance the surface produces itself, so it bypasses albedo, the metallic split and
	// the 1/PI Lambert factor, exactly as PathTrace.comp adds h.emissive on hit. ambientScale must not touch it:
	// that knob compensates for missing bounces in the *reflected* term, and an emitter's own output is not a
	// bounce.
	return s.Emissive + s.Albedo * (1.0 - s.Metallic) * (direct + ambient * ambientScale);
}

#endif // SNOWSTORM_RT_HIT_SHADING_HLSLI
