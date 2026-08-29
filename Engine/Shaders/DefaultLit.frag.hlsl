#include "Include/Engine.hlsli"

// DefaultLit fragment stage: metallic-roughness PBR (Cook-Torrance) + normal mapping + directional
// shadows + split-sum IBL, then exposure/ACES tonemap/sRGB encode. Paired with the shared
// Mesh.vert.hlsl. Was the fragment half of the old combined DefaultLit.hlsl.

static const float PI = 3.14159265359;

// Depth-comparison sampler for hardware PCF shadows (#60): linear-filters the 0/1 pass/fail results of a
// depth compare (SampleCmpLevelZero), giving smooth 2x2 (or wider) PCF in ONE bilinear-cost tap instead of
// manual N-tap Sample()+compare loops. Clamp-to-edge + LessOrEqual (matches the manual currentDepth <=
// storedDepth). Material binding 3 (s3, space1); the C++ material set binds it engine-globally. Declared
// HERE and not in Engine.hlsli on purpose: the full-screen post passes pair with Fullscreen.vert (which
// includes Engine.hlsli) and park their own cbuffer at b3, space1, so a shared-header s3 would collide.
SamplerComparisonState ShadowCmpSampler : register(s3, space1);

// Sample a bindless texture by a (potentially per-instance, non-uniform) index. Every dynamic index
// into the Textures[] array must go through NonUniformResourceIndex() or instanced draws sample
// garbage (the #46 flicker lesson). Centralized here so no call site forgets.
float4 SampleBindless(uint index, float2 uv)
{
	// MipBias (FrameCB) is negative under TAA so jitter fetches a sharper mip each frame: the temporal
	// resolve then reconstructs the detail instead of thin/distant texels flickering between mips. 0 = off.
	return Textures[NonUniformResourceIndex(index)].SampleBias(LinearSampler, uv, MipBias);
}

// Shared shadow factor: 1 = fully lit, 0 = fully shadowed. Reprojects the world position through a light
// matrix, then does a 3x3 PCF compare against a bindless depth texture. `atlasRect` (xy = UV offset,
// zw = UV scale) maps the light's [0,1] UV into its sub-rect of the texture: (0,0,1,1) for a dedicated
// map (the sun), or a tile rect for a spot in the shared atlas. PCF taps are CLAMPED to the rect so a
// tap near a tile edge can't bleed into a neighbour tile. Manual PCF keeps the bindless SAMPLED_IMAGE
// model (no comparison-sampler descriptor). NdotL drives a slope-scaled bias; ShadowStrength lightens.
float SampleShadowFactor(uint texIndex, float4x4 lightViewProj, float4 atlasRect, float3 positionWS, float3 Ng, float NdotL)
{
	// Normal-offset bias (#59): shift the sample point along the geometric normal before projecting into
	// light space, in WORLD units, scaled by the grazing angle (most acne is on surfaces near-parallel to
	// the light, where a depth bias would need to be huge). Moving the comparison point off the
	// self-occluding plane ALONG the surface removes acne without the depth-push peter-panning a larger
	// ShadowBias causes. ShadowNormalOffset is a world-space distance; (1 - NdotL) makes it vanish on
	// light-facing surfaces (which don't self-shadow) and peak at grazing.
	positionWS += Ng * (ShadowNormalOffset * saturate(1.0 - NdotL));

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

	// Hardware PCF (#60): SampleCmpLevelZero does the depth compare + bilinear filtering of the 0/1 results
	// in ONE texture op (a 2x2 comparison-filtered tap), replacing the manual Sample()+compare. The soft
	// path takes a 3x3 grid of these (so 9 HW taps => an effectively wider, smoother 4x4-ish kernel); the
	// hard path is a single tap (still HW-bilinear, so smoother than the old nearest single-sample).
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
				sum += Textures[NonUniformResourceIndex(texIndex)].SampleCmpLevelZero(ShadowCmpSampler, atlasUV, currentDepth);
			}
		}
		visibility = sum / 9.0;
	}
	else
	{
		const float2 atlasUV = clamp(atlasRect.xy + lightUV * atlasRect.zw, rectMin, rectMax);
		visibility = Textures[NonUniformResourceIndex(texIndex)].SampleCmpLevelZero(ShadowCmpSampler, atlasUV, currentDepth);
	}

	return lerp(1.0, visibility, ShadowStrength);
}

#if defined(SS_RAYTRACING) && defined(SS_LIT_INLINE_RT_SHADOWS)
// Record + any-hit cutout alpha test, shared with the AO/GI/reflection passes. Engine.hlsli already declared
// Textures[] (t0, space3), satisfying RTGeometry's contract, so this include must follow it.
#include "Include/RTGeometry.hlsli"
#include "Include/LightSampling.hlsli" // SampleCone / LightConeCos / IGN / STBN, shared with the path tracer

// Reassemble the geometry-table device address from FrameCB's lo/hi halves (0 = table not published yet ->
// the any-hit test falls back to treating hits as solid).
uint64_t GeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

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
	// grazing angles. Scaled by 1e-2 world units, tuned against acne/peter-panning.
	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = L;
	ray.TMin = 0.0;
	ray.TMax = tMax;

	// ACCEPT_FIRST_HIT_AND_END_SEARCH: a shadow ray only needs "is anything in the way", so stop at the first
	// committed hit. Opaque geometry auto-commits; masked instances (FORCE_NON_OPAQUE) surface as candidates
	// and are alpha-tested so a cutout texel lets the ray through instead of casting a solid shadow.
	const uint64_t tableAddr = GeoTableAddress();
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

// Shadow rays per light per frame for the soft path. Compile-time (a cost-class knob, not a live one),
// kept low since per-frame sample rotation + TAA accumulate many effective samples over time.
#define SHADOW_RAY_COUNT 2

// Soft ray-traced shadow (#118): like RayTraceShadow, but instead of one ray straight at the light, shoot
// SHADOW_RAY_COUNT rays sampled uniformly inside the cone the light subtends (half-angle acos(`coneCos`)),
// modelling the light's AREA. Averaging the hits gives a visibility in [0,1] (a penumbra) instead of {0,1}.
// Sharp where the caster is close (small subtended angle), softening with distance, which is the physical
// behaviour. Sampling by SOLID ANGLE rather than in a tangent-plane disk is what makes this agree with the
// path tracer's NEE at any cone width, not only in the small-angle limit. The frame-rotated IGN hash picks
// different directions each frame so TAA smooths the noise. coneCos == 1 reduces exactly to the hard single
// ray. RT permutation only.
// `dimBase` reserves this light two STBN dimensions, so two lights shading the same pixel draw decorrelated
// jitter instead of the same scalar (correlated error does not average down, however many lights there are).
float RayTraceSoftShadow(float3 positionWS, float3 Ng, float3 L, float tMax, float coneCos, float2 pixelPos, uint dimBase)
{
	const uint2 px = uint2(pixelPos);
	// Two decorrelated spatiotemporal blue-noise draws: one offsets the stratified polar stratum, one the
	// azimuth. Shadow.comp draws the same two per traced ray.
	const float jTheta = STBN(px, FrameCounter, dimBase);
	const float jPhi = STBN(px, FrameCounter, dimBase + 1u);

	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;
	const uint64_t tableAddr = GeoTableAddress();

	float visSum = 0.0;
	[unroll] for (int s = 0; s < SHADOW_RAY_COUNT; ++s)
	{
		// Cone sample: polar stratified by ray index (one sample per equal-solid-angle stratum, which the
		// blue-noise offset only jitters WITHIN), azimuth golden-ratio rotated off its own draw.
		const float u1 = (float(s) + jTheta) / float(SHADOW_RAY_COUNT);
		const float u2 = frac(jPhi + float(s) * 0.61803398875);
		const float3 dir = SampleCone(L, coneCos, u1, u2);

		// Part of the light's disk sits below the GEOMETRIC horizon near the terminator; the surface itself
		// occludes those samples, and no ray can report that (the origin is offset above the surface).
		// Counting them lit is the light leak the path tracer avoids by rejecting the same samples in NEE.
		if (dot(Ng, dir) <= 0.0)
		{
			continue;
		}

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = dir;
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

		visSum += (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
	}

	const float visibility = visSum / float(SHADOW_RAY_COUNT);
	return lerp(1.0, visibility, ShadowStrength);
}

// NOTE (#124/#129): the inline RayTraceGI hemisphere gather AND RayTraceReflection (plus the geometry-
// table read + hit-shading helpers they used) were DELETED here: GI runs in a half-res compute pass
// (GI.comp.hlsl) and reflections in a full-res compute pass (Reflection.comp.hlsl), each writing a target
// the forward pass samples by screen UV (GITextureIndex / ReflectionTextureIndex reads in main). The
// shared hit-shading helpers now live in Include/RTHitShading.hlsli (used by both compute passes).
// RayTraceShadow / RayTraceSoftShadow STAY above: the primary sun/point/spot lighting still traces them.
#endif

// Directional-sun shadow: RT ray query (when RTShadowEnabled) or the raster shadow map (dedicated map,
// gated by ShadowMapIndex; 0 = no shadows). `Ng`/`L`/`pixelPos` are only used by the RT path.
//
// The three Sample*Shadow entry points take a `lightIdx` that is unique across ALL lights in the frame (sun 0,
// point 1 + p, spot 1 + MAX_POINT_LIGHTS + s), used only to hand each light its own STBN dimension pair.
float SampleSunShadow(float3 positionWS, float3 Ng, float3 L, float NdotL, float2 pixelPos, uint lightIdx)
{
#if defined(SS_RAYTRACING) && defined(SS_LIT_INLINE_RT_SHADOWS)
	if (RTShadowEnabled != 0)
	{
		// Soft (cone-sampled penumbra) when enabled: the sun's angular radius is already supplied as a
		// cosine, so it feeds the cone sampler directly. Else the hard single ray. Sun is at infinity.
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, 1e30, SunCosThetaMax, pixelPos, lightIdx * 2u);
		}
		return RayTraceShadow(positionWS, Ng, L, 1e30);
	}
#endif
	if (ShadowMapIndex == 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(ShadowMapIndex, LightViewProj, float4(0, 0, 1, 1), positionWS, Ng, NdotL);
}

// Spot shadow: RT ray query (when RTShadowEnabled and this spot casts) or the shared raster atlas at the
// spot's tile. `Ng` = geometric normal (ray offset), `L` = direction to the light, `distToLight` = ray
// length for the RT path. Raster path gated by the atlas index being bound AND the spot having a tile
// (ShadowIndex >= 0); RT path gated by ShadowIndex >= 0 alone (the "this light casts" sentinel).
float SampleSpotShadow(SpotLight spot, float3 positionWS, float3 Ng, float3 L, float distToLight, float NdotL, float2 pixelPos, uint lightIdx)
{
#if defined(SS_RAYTRACING) && defined(SS_LIT_INLINE_RT_SHADOWS)
	if (RTShadowEnabled != 0)
	{
		if (spot.ShadowIndex < 0)
		{
			return 1.0; // this spot doesn't cast
		}
		// Stop just short of the light so the ray can't hit geometry at/behind the light position.
		const float tMax = max(distToLight - 0.05, 0.0);
		// Soft: a source of radius LightSourceRadius at distToLight subtends a cone whose half-angle has
		// sine (radius / distance), so a bigger or closer source gives a wider penumbra.
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, tMax, LightConeCos(LightSourceRadius, distToLight), pixelPos, lightIdx * 2u);
		}
		return RayTraceShadow(positionWS, Ng, L, tMax);
	}
#endif
	if (SpotShadowAtlasIndex == 0 || spot.ShadowIndex < 0)
	{
		return 1.0;
	}
	return SampleShadowFactor(SpotShadowAtlasIndex, spot.ShadowViewProj, spot.ShadowAtlasRect, positionWS, Ng, NdotL);
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
float SamplePointShadow(PointLight light, float3 positionWS, float3 Ng, float3 L, float distToLight, float NdotL, float2 pixelPos, uint lightIdx)
{
#if defined(SS_RAYTRACING) && defined(SS_LIT_INLINE_RT_SHADOWS)
	if (RTShadowEnabled != 0)
	{
		if (light.ShadowSlot < 0)
		{
			return 1.0; // this light doesn't cast
		}
		const float tMax = max(distToLight - 0.05, 0.0);
		if (ShadowSoft != 0)
		{
			return RayTraceSoftShadow(positionWS, Ng, L, tMax, LightConeCos(LightSourceRadius, distToLight), pixelPos, lightIdx * 2u);
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
	return SampleShadowFactor(PointShadowAtlasIndex, payload.Face[face], payload.Rect[face], positionWS, Ng, NdotL);
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
// traced 1-bounce GI): the diffuse indirect becomes scene-derived instead of the constant sky
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

	// #163: when RT GI is active the diffuse indirect above is scene-traced + occluded (giDiffuse), but this
	// env-cube specular stays un-occluded. On ROUGH surfaces its wide lobe behaves like a second, un-occluded
	// ambient that overlaps the occluded diffuse GI and over-fills shadows vs the path-traced reference (the
	// whole measured RT-GI-on brightness gap traced to this term). Real engines feed rough specular from the
	// same occluded scene radiance (Lumen's radiance cache), not an un-occluded skybox; lacking that here,
	// fade the rough env-cube specular by roughness so it defers to the traced GI. Smooth surfaces keep it
	// (a genuine mirror-ish reflection, and the RT reflection below replaces it for roughness < cutoff).
	if (useGIDiffuse != 0)
	{
		specular *= 1.0 - GISpecAmbientFade * saturate(roughness);
	}

	// RT reflections (#129): for smooth surfaces, blend in the traced reflection of the ACTUAL scene. The
	// trace now runs in a SEPARATE full-res pass (ReflectionPass) that writes raw reflected radiance into a
	// buffer (ReflectionTextureIndex), so it can be temporally accumulated to kill the few-ray shimmer,
	// unlike the old inline trace. Here the forward pass just SAMPLES that buffer by screen UV and applies
	// the same weights: reflWeight is a PURE roughness falloff (rough surfaces stay on the cheap cube;
	// ReflMaxRoughness is the cutoff), specWeight is the split-sum Fresnel/BRDF, and ReflIntensity is the RT
	// term's OWN brightness dial (decoupled from IBLIntensity: real scene light, not baked ambient).
	if (ReflectionTextureIndex != 0 && roughness < ReflMaxRoughness)
	{
		const float2 reflUv = pixelPos / max(RenderTargetSize, float2(1.0, 1.0)); // plain UV: effects jittered like geometry now
		const float3 rt = Textures[NonUniformResourceIndex(ReflectionTextureIndex)].SampleLevel(LinearSampler, reflUv, 0).rgb;
		const float reflWeight = saturate(1.0 - roughness / max(ReflMaxRoughness, 1e-3));
		const float3 specularRT = rt * specWeight * ReflIntensity;
		specular = lerp(specular, specularRT, reflWeight);
	}

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

// Cook-Torrance split into diffuse + specular (the same terms as ShadePBR, unshadowed). The stochastic-shadow
// forward path (Option B) needs them separately: the colored diffuse direct comes from the denoised RT-shadow
// irradiance buffer (per-light color + shadow correct), while specular is summed here and shadowed by a cheap
// aggregate visibility. `radiance` is color*intensity pre-attenuation; caller applies attenuation/cone.
void ShadePBRSplit(float3 N, float3 V, float3 L, float3 F0, float3 albedo, float metallic, float roughness,
                   float3 radiance, out float3 outDiffuse, out float3 outSpecular)
{
	const float3 H = normalize(V + L);
	const float NdotL = max(dot(N, L), 0.0);
	const float Dd = DistributionGGX(N, H, roughness);
	const float G = GeometrySmith(N, V, L, roughness);
	const float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
	const float specGeom = (Dd * G) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-4); // D*G/(4 NoV NoL), Fresnel-free
	const float3 kd = (1.0 - F) * (1.0 - metallic);
	outDiffuse = kd * albedo / PI * radiance * NdotL;
	outSpecular = specGeom * F * radiance * NdotL; // sharp full-res highlight with correct per-light F
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
	// its argument is < 0. Opaque-pass masking only: no blending/sorting. Off (AlphaMaskEnabled == 0) for
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

	// Ray-traced ambient occlusion (#126): RTAO now traces in a separate half-res compute pass over the
	// depth+normal G-buffer and is bilateral-upsampled to a full-res target (RTAOTextureIndex), exactly like
	// half-res GI below. Here the forward pass just SAMPLES that scalar factor by screen UV and folds it into
	// `ao` so both the IBL branch and the analytic-hemisphere fallback inherit it. 0 index = AO off this frame
	// -> `ao` keeps just the material AO map. Replaces the old inline per-pixel RayTraceAO (deleted).
	if (RTAOTextureIndex != 0)
	{
		// Subtract the TAA jitter (UV units) so the AO is sampled at the SAME sub-pixel spot the jittered
		// geometry occupies, giving TAA sub-pixel variation to average, so half-res AO edges anti-alias.
		const float2 aoUv = i.PositionCS.xy / max(RenderTargetSize, float2(1.0, 1.0)); // plain UV: effects jittered like geometry now
		ao *= Textures[NonUniformResourceIndex(RTAOTextureIndex)].SampleLevel(LinearSampler, aoUv, 0).r;
	}

	// Debug view (#126): the AO debug view (DebugView == 2) now reads the raw half-res AO target directly in
	// the tonemap pass (Tonemap.frag.hlsl DebugMode 4), same as half-res GI, so there's no in-shader AO
	// debug branch here anymore (it would be overwritten by that tonemap pass regardless).

	// Debug view 3 (#129): output the raw reflection buffer (the separate ReflectionPass's traced+shaded
	// radiance) so the reflection signal is verifiable on screen independent of the specular blend. Reads the
	// full-res reflection target by screen UV. Gated by DebugReflections (DebugView == 3); if reflections are
	// off / no table this frame the index is 0 -> black.
	if (DebugReflections != 0)
	{
		float3 refl = float3(0, 0, 0);
		if (ReflectionTextureIndex != 0)
		{
			const float2 reflUv = i.PositionCS.xy / max(RenderTargetSize, float2(1.0, 1.0));
			refl = Textures[NonUniformResourceIndex(ReflectionTextureIndex)].SampleLevel(LinearSampler, reflUv, 0).rgb;
		}
		return float4(refl, 1.0);
	}

	// Half-res RT diffuse GI (#124): the GI hemisphere gather now runs in a separate compute pass at
	// render.gi.scale and is bilateral-upsampled to a full-res target (GITextureIndex). Here the forward
	// pass just SAMPLES that irradiance by screen UV and multiplies the receiver albedo in (kept out of the
	// half-res buffer so the upsample can't blur albedo edges). 0 index = GI off this frame -> no indirect.
	// Replaces the old inline per-pixel RayTraceGI (deleted): GI no longer traces in the forward shader.
	float3 giIndirect = float3(0, 0, 0);
	if (GITextureIndex != 0)
	{
		const float2 giUv = i.PositionCS.xy / max(RenderTargetSize, float2(1.0, 1.0)); // plain UV: effects jittered like geometry now
		const float3 giIrradiance = Textures[NonUniformResourceIndex(GITextureIndex)].SampleLevel(LinearSampler, giUv, 0).rgb;
		giIndirect = giIrradiance * albedo; // irradiance * receiver albedo = diffuse indirect response
	}
	// Debug view 4 = GI: output the (upsampled) indirect term for tuning intensity/range against the raw
	// signal. Now works in the non-RT permutation too, since it's just a texture read.
	if (DebugGI != 0)
	{
		return float4(giIndirect, 1.0);
	}

	// Stochastic RT direct lighting (Option B / MegaLights-lite): when the half-res shadow pass ran
	// (SunShadowTextureIndex != 0), it wrote the COLORED shadowed direct IRRADIANCE D = Σ_i radiance_i·NdotL_i·vis_i
	// over ALL lights (per-light color + shadow correct, NO albedo) into .rgb, temporally-accumulated + à-trous-
	// denoised + upsampled. The forward multiplies full-res albedo here (albedo factors out of the diffuse sum, so
	// it stays out of the denoised buffer -> no albedo blur, the GI pattern). Specular is summed sharp below and
	// shadowed by a cheap aggregate visibility. 0 index -> inline/raster per-light path (loops below trace).
	float3 shadowIrr = float3(0, 0, 0); // denoised colored shadowed direct DIFFUSE irradiance (Option B)
	float3 specVis = float3(1, 1, 1);   // denoised per-channel specular VISIBILITY [0,1] from the pass; modulates the sharp full-res specular
	bool useShadowTex = false;
	bool useShadowSpec = false;
	if (SunShadowTextureIndex != 0)
	{
		const float2 shUv = i.PositionCS.xy / max(RenderTargetSize, float2(1.0, 1.0)); // plain UV: effect buffers jittered like geometry
		shadowIrr = Textures[NonUniformResourceIndex(SunShadowTextureIndex)].SampleLevel(LinearSampler, shUv, 0).rgb;
		useShadowTex = true;
		// Specular visibility twin (MegaLights/NRD): the pass emits a SMOOTH per-channel specular shadow ratio (the
		// specular BRDF shape cancels in shadowed/unshadowed), so denoising it never blurs a sharp glossy highlight;
		// the highlight itself stays full-res in specSum below. 0 index -> fall back to the diffuse-weighted grey-vis.
		if (ShadowSpecTextureIndex != 0)
		{
			specVis = Textures[NonUniformResourceIndex(ShadowSpecTextureIndex)].SampleLevel(LinearSampler, shUv, 0).rgb;
			useShadowSpec = true;
		}
	}

	float3 Lo = float3(0, 0, 0);
	float3 unshadowedIrr = float3(0, 0, 0); // Σ radiance_i·NdotL_i (unshadowed): diffuse albedo scale + fallback specular vis
	float3 specSum = float3(0, 0, 0);       // Σ unshadowed specular WITH Fresnel (the sharp full-res highlight)

	// --- Directional lights (the sun).
	const int count = clamp(LightCount, 0, MAX_DIRECTIONAL_LIGHTS);
	[loop] for (int l = 0; l < count; ++l)
	{
		const float3 L = normalize(-DirectionalLights[l].Direction);
		const float3 radiance = DirectionalLights[l].Color * DirectionalLights[l].Intensity;
		const float ndl = max(dot(N, L), 0.0);
		// Stochastic (useShadowTex): the diffuse comes from the denoised irradiance D; here we only need the
		// unshadowed irradiance (for the albedo scale) + the sharp specular. Inline: light 0 traces its own shadow.
		if (useShadowTex)
		{
			float3 diff, spec;
			ShadePBRSplit(N, V, L, F0, albedo, metallic, roughness, radiance, diff, spec);
			unshadowedIrr += radiance * ndl;
			specSum += spec;
		}
		else
		{
			const float shadow = (l == 0) ? SampleSunShadow(i.PositionWS, N, L, ndl, i.PositionCS.xy, 0u) : 1.0;
			Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * shadow;
		}
	}

	// --- Point lights: inverse-square falloff with a smooth windowed cutoff at Range (UE4/Frostbite).
	const int pointCount = clamp(PointCount, 0, MAX_POINT_LIGHTS);
	[loop] for (int p = 0; p < pointCount; ++p)
	{
		const float3 toLight = PointLights[p].Position - i.PositionWS;
		const float dist = length(toLight);
		const float range = max(PointLights[p].Range, 1e-4);
		if (dist >= range) // range cull (lossless: falloff is 0 at d >= range)
		{
			continue;
		}

		const float3 L = toLight / max(dist, 1e-4);
		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);
		const float ndl = max(dot(N, L), 0.0);
		const float3 radiance = PointLights[p].Color * PointLights[p].Intensity * atten;

		if (useShadowTex)
		{
			float3 diff, spec;
			ShadePBRSplit(N, V, L, F0, albedo, metallic, roughness, radiance, diff, spec);
			unshadowedIrr += radiance * ndl;
			specSum += spec;
		}
		else
		{
			const float pointShadow = SamplePointShadow(PointLights[p], i.PositionWS, N, L, dist, ndl, i.PositionCS.xy, uint(1 + p));
			Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * pointShadow;
		}
	}

	// --- Spot lights: point attenuation * smooth cone falloff.
	const int spotCount = clamp(SpotCount, 0, MAX_SPOT_LIGHTS);
	[loop] for (int s = 0; s < spotCount; ++s)
	{
		const float3 toLight = SpotLights[s].Position - i.PositionWS;
		const float dist = length(toLight);
		const float range = max(SpotLights[s].Range, 1e-4);
		if (dist >= range) // range cull
		{
			continue;
		}

		const float3 L = toLight / max(dist, 1e-4);
		const float cosAngle = dot(-L, SpotLights[s].Direction);
		if (cosAngle <= SpotLights[s].CosOuter) // cone cull
		{
			continue;
		}

		const float window = pow(saturate(1.0 - pow(dist / range, 4.0)), 2.0);
		const float atten = window / max(dist * dist, 1e-4);
		const float denom = max(SpotLights[s].CosInner - SpotLights[s].CosOuter, 1e-4);
		const float cone = pow(saturate((cosAngle - SpotLights[s].CosOuter) / denom), 2.0);
		const float ndl = max(dot(N, L), 0.0);
		const float3 radiance = SpotLights[s].Color * SpotLights[s].Intensity * atten * cone;

		if (useShadowTex)
		{
			float3 diff, spec;
			ShadePBRSplit(N, V, L, F0, albedo, metallic, roughness, radiance, diff, spec);
			unshadowedIrr += radiance * ndl;
			specSum += spec;
		}
		else
		{
			const float spotShadow = SampleSpotShadow(SpotLights[s], i.PositionWS, N, L, dist, ndl, i.PositionCS.xy, uint(1 + MAX_POINT_LIGHTS + s));
			Lo += ShadePBR(N, V, L, F0, albedo, metallic, roughness, radiance) * spotShadow;
		}
	}

	// Stochastic path (Option B): reconstruct the shadowed direct term from the denoised colored irradiance.
	// Diffuse = albedo/π·(1-metallic)·shadowedIrr, with per-light color + shadow carried correctly by D (fixes the
	// grey-aggregate-ratio color error). ShadowStrength lerps between unshadowed (0) and D (1); clamp <= unshadowed
	// so RIS/denoiser overshoot can't brighten past the unshadowed light (the "overly bright" guard). Specular is
	// sharp (out of the denoiser) but shadowed by an aggregate grey visibility (specular color error is minor).
	if (useShadowTex)
	{
		const float3 shadowedIrr = min(lerp(unshadowedIrr, shadowIrr, ShadowStrength), unshadowedIrr);
		const float3 diffuseDirect = albedo * (1.0 / PI) * (1.0 - metallic) * shadowedIrr;
		if (useShadowSpec)
		{
			// Specular VISIBILITY: the pass already computed the smooth per-channel [0,1] specular shadow ratio (the
			// BRDF shape cancels there), so just modulate the SHARP full-res specSum by it: highlight stays crisp
			// (specSum, correct per-light Fresnel), only the shadow term is denoised. No highlight smear on glossy
			// surfaces and no grazing-angle dimming (deriving the ratio from a denoised specular RADIANCE did both).
			Lo = diffuseDirect + specSum * lerp(float3(1, 1, 1), specVis, ShadowStrength);
		}
		else
		{
			// Fallback (spec buffer off): shadow the summed specular by the diffuse-weighted grey visibility.
			const float3 lw = float3(0.2126, 0.7152, 0.0722);
			const float uL = dot(unshadowedIrr, lw);
			const float visGrey = (uL > 1e-4) ? saturate(dot(shadowedIrr, lw) / uL) : 1.0;
			Lo = diffuseDirect + specSum * visGrey;
		}
	}

	// 1-bounce RT diffuse GI (#118): when active, the traced indirect REPLACES the DIFFUSE ambient (Lumen/
	// RTXGI model): the diffuse fill becomes scene-derived (color bleed, contact fill) instead of the
	// constant sky guess. Specular ambient (env cube / RT reflection) is unaffected. Off => the baked/
	// analytic diffuse as before.
	uint useGI = 0;
	float3 giDiffuse = float3(0, 0, 0);
	if (GITextureIndex != 0)
	{
		useGI = 1;
		giDiffuse = giIndirect; // upsampled GI irradiance * receiver albedo (see gather above)
	}

	// Ambient: prefer split-sum IBL (baked from the sky) when available, since metals reflect the
	// environment and specular picks up sky color. Falls back to the analytic hemisphere ambient (same
	// zenith/horizon/ground colors the sky shows) when IBL isn't baked, so the look degrades gracefully.
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
	// now lives once in the post-process pass (Tonemap.frag.hlsl, #53), which samples this target; the
	// mesh and sky shaders no longer tonemap inline.
	return float4(color, BaseColor.a);
}