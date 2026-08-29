// Half-resolution STOCHASTIC direct-shadow pass (MegaLights-lite), compute stage. Replaces the inline
// per-light SampleSunShadow/SamplePointShadow/SampleSpotShadow RayQueries in DefaultLit (the dominant Forward
// RT cost on a many-light scene) with ONE importance-sampled shadow ray per half-res pixel, regardless of
// light count. Mirrors the AO/GI half-res pattern (few noisy samples -> temporal -> SVGF denoise -> upsample).
//
// METHOD (aggregate shadow ratio): per half-res pixel, weight every in-range light by its UNSHADOWED
// contribution w_i = luma(color*intensity) * attenuation * cone * NdotL. Weighted-reservoir-sample ONE light y
// with P(y) proportional to w_i (one pass, no per-light storage), then trace ONE shadow ray to y -> vis(y).
// Because y ~ w_i, E[vis(y)] = Sum_i(w_i * vis_i) / Sum_i(w_i) = the contribution-weighted AGGREGATE SHADOW
// RATIO, so a SINGLE sample is an UNBIASED estimator of it. The temporal+denoise stages converge it; the
// forward computes full-res UNSHADOWED direct lighting and multiplies by this denoised ratio (full-res shading
// preserved). Non-casting lights stay in the pool (vis=1, no ray) so the ratio is correct; only a sampled
// CASTING light traces. Output = one half-res Texture2D (scalar ratio estimate in .r), bindless-friendly.
//
// Compiled only in the SS_RAYTRACING permutation (RayQuery). SceneTLAS = set 3 (gap-filled by the compute
// pipeline builder); this pass's own inputs (G-buffer, output UAV, params) = set 0. Slim tracer-only light
// data (NOT the raster shadow matrices) rides the CB; keep it in lockstep with RTShadowPass.cpp.

#define SHADOW_MAX_DIR 4
#define SHADOW_MAX_POINT 16
#define SHADOW_MAX_SPOT 16

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GBufferNormal : register(t0, space0);        // .xy = oct GEOMETRIC normal, .z = roughness (ray origin + GGX)
Texture2D<float4> GBufferShadingNormal : register(t5, space0); // .xy = oct NORMAL-MAPPED normal (NdotL + specular BRDF, matches DefaultLit)
Texture2D<float> GBufferDepth : register(t4, space0);          // fp32 NDC depth (D32 attachment)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ShadowOut : register(u1, space0);     // .rgb = colored shadowed DIFFUSE irradiance D, .a = penumbra guide
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ShadowSpecOut : register(u6, space0); // .rgb = demodulated shadowed SPECULAR (GGX D*G, no Fresnel); forward re-applies F0
SamplerState LinearSampler : register(s2, space0); // wrapping sampler for the cutout alpha lookup (any-hit test)

cbuffer ShadowCB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	uint2 OutSize;        // half-res dispatch dimensions
	float NormalBias;     // world-space normal offset for the ray origin (acne/peter-pan guard)
	uint FrameCounter;    // per-frame sample rotation (the temporal pass converges the 1 ray/pixel)

	uint DirCount;           // active directional lights (<= SHADOW_MAX_DIR)
	uint PointCount;         // active point lights (<= SHADOW_MAX_POINT)
	uint SpotCount;          // active spot lights (<= SHADOW_MAX_SPOT)
	uint ReflGeoTableAddrLo; // device address (lo) of the per-instance geometry table, for the cutout alpha test

	uint DirCastMask;   // bit i set => dir light i casts a shadow (else vis=1, no ray)
	uint PointCastMask; // bit i => point light i casts
	uint SpotCastMask;  // bit i => spot light i casts
	uint SoftEnabled;   // 1 => jitter the chosen ray within the light's area (soft penumbra); 0 => hard ray

	float SunCosThetaMax;    // cos(sun angular RADIUS) -> the cone the soft jitter samples (1 = delta light)
	float SourceRadius;      // local-light source radius (world units); the cone it subtends narrows with distance
	uint RayCount;           // stochastic samples/pixel (render.shadows.rays): more = less variance, ~linear cost
	uint ReflGeoTableAddrHi; // device address (hi) of the per-instance geometry table

	uint UseLogWeight;      // 1 = log(1+luma) perceptual importance weight (downweights strong occluded lights); 0 = linear luma
	float3 CameraPosition; // world-space camera pos, for V = normalize(camPos - worldPos) in the specular BRDF

	uint UseSpecImportance; // 1 = combined diffuse+spec RIS target (samples specular-dominant lights too); 0 = diffuse-only
	float3 _PadSpecImp;     // pad to a 16-byte row

	// Slim tracer + importance params (16-byte rows). Option B: each light carries its full RGB radiance
	// (color*intensity) so the pass can accumulate COLORED shadowed irradiance; the importance weight is the
	// luma of that radiance, computed here.
	float4 DirData[SHADOW_MAX_DIR];         // xyz = normalized dir TO light, w unused
	float4 DirColor[SHADOW_MAX_DIR];        // xyz = color*intensity (radiance, no attenuation)
	float4 PointPosRange[SHADOW_MAX_POINT]; // xyz = world pos, w = range
	float4 PointColor[SHADOW_MAX_POINT];    // xyz = color*intensity
	float4 SpotPosRange[SHADOW_MAX_SPOT];   // xyz = world pos, w = range
	float4 SpotDirCos[SHADOW_MAX_SPOT];     // xyz = spot forward axis, w = cos(outer half-angle)
	float4 SpotColorInner[SHADOW_MAX_SPOT]; // xyz = color*intensity, w = cos(inner half-angle)
};

float Luma3(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// GGX specular geometry (Cook-Torrance D*G, NO Fresnel) — copied from DefaultLit's ShadePBR so the pass's
// per-sample specular matches the forward. Fresnel is DEMODULATED here (re-applied as F0 in the forward), so the
// denoised specular buffer carries no material term -> no albedo/F0 blur across material edges.
static const float SS_PI = 3.14159265358979;
float SpecDistributionGGX(float3 N, float3 H, float roughness)
{
	const float a = roughness * roughness;
	const float a2 = a * a;
	const float nh = max(dot(N, H), 0.0);
	const float d = nh * nh * (a2 - 1.0) + 1.0;
	return a2 / max(SS_PI * d * d, 1e-5);
}
float SpecGeometrySchlickGGX(float nv, float roughness)
{
	const float r = roughness + 1.0;
	const float k = (r * r) / 8.0; // direct-lighting remap (matches DefaultLit)
	return nv / (nv * (1.0 - k) + k);
}
float SpecGeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
	return SpecGeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * SpecGeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

// Cap on the specular importance boost: a near-mirror light's specGeom peaks very high, so left unbounded it
// would spike its reservoir weight and starve the diffuse-dominant lights (noisy diffuse D). 8 lets specular
// raise a light's weight up to ~9x its diffuse luma — enough to sample glossy highlights well without starving.
static const float SPEC_IMPORTANCE_CAP = 8.0;

// Importance weight for the reservoir AND the RIS normalization (must match in both, or the estimate biases).
// UseLogWeight = log(1+luma) perceptual weighting downweights very strong lights so a strong-but-occluded light
// can't dominate the per-pixel selection and drag the aggregate dark where a weaker VISIBLE light should light
// the pixel (UE5 MegaLights uses log weighting for this exact "strong occluded light" issue, SIGGRAPH 2025).
// UseSpecImportance boosts the diffuse luma by the light's (capped) specular response so the SAME reservoir also
// samples the specular-dominant light well -> lower specular-visibility variance. Off = diffuse-only importance.
float ImportanceWeight(float3 radNdotL, float3 N, float3 V, float3 L, float roughness)
{
	float total = Luma3(radNdotL);
	if (UseSpecImportance != 0u)
	{
		const float3 H = normalize(V + L);
		const float nl = max(dot(N, L), 0.0);
		const float nv = max(dot(N, V), 0.0);
		const float dg = SpecDistributionGGX(N, H, roughness) * SpecGeometrySmith(N, V, L, roughness);
		const float specBoost = min(dg / max(4.0 * nv * nl, 1e-4), SPEC_IMPORTANCE_CAP);
		total *= (1.0 + specBoost);
	}
	return (UseLogWeight != 0u) ? log(1.0 + total) : total;
}

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
Texture2D Textures[] : register(t0, space3); // bindless albedo (for the cutout any-hit alpha test)
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

// Record + any-hit cutout alpha test, shared with the AO/GI/reflection passes + the inline shadow path.
// Textures[] above satisfies RTGeometry's contract, so this include must follow it.
#include "Include/RTGeometry.hlsli"
#include "Include/GBufferEncode.hlsli"  // oct-normal decode + IsSky
#include "Include/LightSampling.hlsli"  // SampleCone / LightConeCos / IGN / STBN, shared with the path tracer

// Reassemble the geometry-table device address from the CB lo/hi halves (0 = table not published this frame ->
// the any-hit test treats every hit as solid, matching AO's fallback).
uint64_t GeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

// Windowed inverse-square attenuation, matching DefaultLit's point/spot falloff exactly.
float FalloffWindow(float dist, float range)
{
	const float t = saturate(1.0 - pow(dist / range, 4.0));
	return (t * t) / max(dist * dist, 1e-4);
}

// One-pass weighted reservoir selection over ALL in-range lights, weight = unshadowed contribution. `frame`
// animates the spatiotemporal blue noise; `dimBase` decorrelates this sample's draws from the other K samples'
// (each light in the stream reads dimBase + lightIdx). Calling this K times draws K lights ~proportional to
// contribution -> averaging their visibility is a K-sample estimate of the aggregate shadow ratio (variance
// ~1/K). Returns false when no light contributes here; else fills the chosen light's tracer params (direction
// TO light, ray tMax, cos of the half-angle its cone subtends, whether it casts a shadow).
bool SelectLight(uint2 px, float3 positionWS, float3 N, float3 V, float roughness, uint frame, uint dimBase, out float3 outL,
                 out float outTMax, out float outConeCos, out bool outCasts, out float3 outRadNdotL, out float outWSum)
{
	float wSum = 0.0;
	outL = float3(0, 0, 1);
	outTMax = 1e30;
	outConeCos = 1.0; // delta light until a selection widens it
	outCasts = false;
	outRadNdotL = float3(0, 0, 0); // chosen light's COLORED radiance * NdotL (pre-visibility), for the RIS estimate
	outWSum = 0.0;
	bool have = false;
	uint lightIdx = 0; // global stream index, for decorrelated randoms

	// Directional (sun and any extra suns): infinite distance, no attenuation.
	for (uint d = 0; d < DirCount; ++d, ++lightIdx)
	{
		const float3 L = DirData[d].xyz;
		const float3 radNdotL = DirColor[d].xyz * max(dot(N, L), 0.0);
		const float w = ImportanceWeight(radNdotL, N, V, L, roughness); // combined diffuse+spec importance (render.shadows.importance.*)
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = 1e30;
			outConeCos = SunCosThetaMax;
			outCasts = (DirCastMask & (1u << d)) != 0u;
			outRadNdotL = radNdotL;
			have = true;
		}
	}

	// Point lights: windowed inverse-square falloff, range-culled.
	for (uint p = 0; p < PointCount; ++p, ++lightIdx)
	{
		const float3 toLight = PointPosRange[p].xyz - positionWS;
		const float range = PointPosRange[p].w;
		const float dist = length(toLight);
		if (dist >= range)
		{
			continue;
		}
		const float3 L = toLight / max(dist, 1e-4);
		const float3 radNdotL = PointColor[p].xyz * FalloffWindow(dist, range) * max(dot(N, L), 0.0);
		const float w = ImportanceWeight(radNdotL, N, V, L, roughness);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeCos = LightConeCos(SourceRadius, dist); // the source sphere subtends a wider cone up close
			outCasts = (PointCastMask & (1u << p)) != 0u;
			outRadNdotL = radNdotL;
			have = true;
		}
	}

	// Spot lights: point falloff * smooth cone, range + cone culled.
	for (uint s = 0; s < SpotCount; ++s, ++lightIdx)
	{
		const float3 toLight = SpotPosRange[s].xyz - positionWS;
		const float range = SpotPosRange[s].w;
		const float dist = length(toLight);
		if (dist >= range)
		{
			continue;
		}
		const float3 L = toLight / max(dist, 1e-4);
		const float cosAngle = dot(-L, SpotDirCos[s].xyz);
		const float cosOuter = SpotDirCos[s].w;
		if (cosAngle <= cosOuter)
		{
			continue;
		}
		const float denom = max(SpotColorInner[s].w - cosOuter, 1e-4); // .w = cos(inner)
		const float cone = pow(saturate((cosAngle - cosOuter) / denom), 2.0);
		const float3 radNdotL = SpotColorInner[s].xyz * FalloffWindow(dist, range) * cone * max(dot(N, L), 0.0);
		const float w = ImportanceWeight(radNdotL, N, V, L, roughness);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeCos = LightConeCos(SourceRadius, dist);
			outCasts = (SpotCastMask & (1u << s)) != 0u;
			outRadNdotL = radNdotL;
			have = true;
		}
	}

	outWSum = wSum;
	return have;
}

// Trace one (optionally area-jittered) shadow ray toward the chosen light. Returns visibility (1 = lit).
// outHitT = distance to the nearest occluder on a hit (world units, for the SIGMA-style penumbra-aware
// denoiser kernel), or -1 on a miss (no occluder -> excluded from the mean).
float TraceShadow(uint2 px, uint frame, uint dimBase, float3 positionWS, float3 N, float3 L, float tMax, float coneCos, out float outHitT)
{
	float3 dir = L;
	// Soft shadows: jitter the ray uniformly within the cone the light subtends. The per-sample + temporal +
	// à-trous averaging converges the penumbra (MegaLights/RTXDI area-light approach).
	if (SoftEnabled != 0u && coneCos < 1.0)
	{
		// Jitter axes at high dims (past the light-stream dims for this sample) so they don't collide.
		const float u1 = STBN(px, frame, dimBase + 48u);
		const float u2 = STBN(px, frame, dimBase + 49u);
		dir = SampleCone(L, coneCos, u1, u2);

		// Near the terminator part of the light's disk sits below the GEOMETRIC horizon; those samples are
		// occluded by the surface itself, which no ray can report (the ray origin is offset above it). Counting
		// them as lit is the light leak the path tracer avoids by rejecting the same samples in its NEE loop.
		if (dot(N, dir) <= 0.0)
		{
			outHitT = -1.0; // self-occlusion, not a scene occluder: no distance to feed the penumbra kernel
			return 0.0;
		}
	}

	RayDesc ray;
	ray.Origin = positionWS + N * NormalBias + L * 0.01;
	ray.Direction = dir;
	ray.TMin = 0.0;
	ray.TMax = tMax;

	// ACCEPT_FIRST_HIT: a shadow ray only needs "is anything in the way". Opaque geometry auto-commits; masked
	// (glTF MASK / FORCE_NON_OPAQUE) instances surface as candidates and are ALPHA-TESTED so a cutout texel
	// (foliage leaf gap, chain link) lets the ray through instead of casting a solid shadow. Previously this used
	// RAY_FLAG_CULL_NON_OPAQUE + a single Proceed(), which discarded ALL cutout geometry -> foliage/thin cutout
	// objects cast no shadow at all (the accuracy bug). Now matches the inline path / AO / GI.
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
	if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		outHitT = q.CommittedRayT(); // nearest occluder distance -> penumbra size guide
		return 0.0;                  // shadowed
	}
	outHitT = -1.0; // no occluder
	return 1.0;     // lit
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// POINT-fetch the full-res G-buffer at the nearest texel — never bilinear (a linear tap blends depth across
	// silhouettes -> world position in mid-air -> shadow that bleeds past the edge).
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float4 gbuf = GBufferNormal.Load(int3(gbTexel, 0));
	const float4 gbufShade = GBufferShadingNormal.Load(int3(gbTexel, 0));
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r;

	// Sky / no geometry -> no direct irradiance (0). DefaultLit doesn't shade sky pixels (the sky pass does),
	// so this only keeps the denoiser/upsample from bleeding a bogus value in from the background.
	if (IsSky(depth))
	{
		ShadowOut[id.xy] = float4(0, 0, 0, 0);
		ShadowSpecOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	const float2 ndc = uv * 2.0 - 1.0;
	const float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;
	// Ng (geometric) offsets the ray origin (acne guard); Ns (shading/normal-mapped) drives NdotL + the specular
	// BRDF so the pass matches DefaultLit's shading. Roughness rides the geometric G-buffer .z (PackGBuffer).
	const float3 Ng = DecodeNormalOct(gbuf.xy);
	const float3 Ns = DecodeNormalOct(gbufShade.xy);
	const float roughness = clamp(gbuf.z, 0.04, 1.0);
	const float3 V = normalize(CameraPosition - positionWS);

	// --- K-sample RIS estimate of the COLORED shadowed direct irradiance D = Σ_i radiance_i·NdotL_i·vis_i
	// (Option B). Each sample importance-samples one light y ∝ luma-contribution and traces one ray; the RIS
	// estimate of the whole sum is radiance_y·NdotL_y·vis_y · wSum / contrib_y (contrib_y = luma of that colored
	// term). Averaging K samples converges to D. The forward multiplies full-res albedo/π·(1-metallic) — albedo
	// factors out of the diffuse sum, so it stays out of this half/full-res buffer (no albedo blur), the GI
	// pattern. A non-casting chosen light contributes with vis = 1 (lit); no-light -> 0.
	const uint rayCount = max(RayCount, 1u);
	float3 Dsum = float3(0, 0, 0);
	// Specular VISIBILITY (not radiance): accumulate the colored specular-contribution-weighted SHADOWED and
	// UNSHADOWED sums; their per-channel ratio is a SMOOTH shadow ratio [0,1] (the specular BRDF shape cancels), so
	// it denoises cleanly at ANY roughness. Per-channel (not grey) so a colored light occluded while another isn't
	// shifts the highlight hue correctly. Denoising the specular RADIANCE instead blurred sharp glossy highlights.
	float3 specShadowed = float3(0, 0, 0);
	float3 specUnshadowed = float3(0, 0, 0);
	float hitTSum = 0.0;  // sum of nearest-occluder distances over the OCCLUDED rays (world units)
	uint hitCount = 0u;   // how many rays hit — the mean is over hits only (misses carry no penumbra info)
	[loop] for (uint s = 0; s < rayCount; ++s)
	{
		// This sample's dimension block for the spatiotemporal blue noise: lights read dimBase + lightIdx (stream
		// <= 36), the soft-jitter axes read dimBase + 48/49; stride 64 keeps the K samples' blocks disjoint. The
		// FRAME (not the sample) is the golden-ratio-animated axis, so the temporal accumulation is low-discrepancy.
		const uint dimBase = s * 64u;
		float3 L;
		float tMax;
		float coneCos;
		bool casts;
		float3 radNdotL; // chosen light's colored radiance*NdotL (pre-visibility)
		float wSum;      // Σ luma-contribution over all in-range lights (the RIS normalization)
		if (!SelectLight(id.xy, positionWS, Ns, V, roughness, FrameCounter, dimBase, L, tMax, coneCos, casts, radNdotL, wSum))
		{
			continue; // no contributing light here -> 0 irradiance from this sample
		}
		float vis = 1.0; // non-casting chosen light is fully lit (no ray)
		if (casts)
		{
			float hitT;
			vis = TraceShadow(id.xy, FrameCounter, dimBase, positionWS, Ng, L, tMax, coneCos, hitT);
			if (hitT >= 0.0)
			{
				hitTSum += hitT;
				++hitCount;
			}
		}
		// RIS: estimate of the FULL colored sum from this one sample = radNdotL·vis · wSum / contrib_y.
		const float contribY = ImportanceWeight(radNdotL, Ns, V, L, roughness); // MUST match SelectLight's weight or the RIS estimate biases
		if (contribY > 1e-6)
		{
			const float risWnoVis = wSum / contribY; // 1/p RIS weight (no visibility)
			const float risW = vis * risWnoVis;
			Dsum += radNdotL * risW;
			// Colored specular contribution of the chosen light: specGeom * radiance*NsDotL (Fresnel-free). Accumulate
			// it into the SHADOWED (×vis) and UNSHADOWED sums; specVis = shadowed/unshadowed is the pure specular
			// visibility, and specGeom·radiance·NsDotL cancels -> no highlight shape in the denoised signal.
			const float3 H = normalize(V + L);
			const float nl = max(dot(Ns, L), 0.0);
			const float nv = max(dot(Ns, V), 0.0);
			const float dg = SpecDistributionGGX(Ns, H, roughness) * SpecGeometrySmith(Ns, V, L, roughness);
			const float3 specContrib = (dg / max(4.0 * nv * nl, 1e-4)) * radNdotL; // colored (Fresnel-free)
			specShadowed += specContrib * risW;
			specUnshadowed += specContrib * risWnoVis;
		}
	}

	const float3 D = Dsum / float(rayCount); // colored shadowed direct irradiance (no albedo)
	// Per-channel specular shadow ratio [0,1] (num/den normalizes out the 1/rayCount): forward multiplies the sharp
	// full-res specular by it. 1 (fully lit) where a channel has no specular (its specSum is ~0 too, so the value
	// there is moot); guarding to 1 instead of 0 keeps a tiny residue from wrongly darkening.
	float3 specVis;
	specVis.r = (specUnshadowed.r > 1e-8) ? saturate(specShadowed.r / specUnshadowed.r) : 1.0;
	specVis.g = (specUnshadowed.g > 1e-8) ? saturate(specShadowed.g / specUnshadowed.g) : 1.0;
	specVis.b = (specUnshadowed.b > 1e-8) ? saturate(specShadowed.b / specUnshadowed.b) : 1.0;
	// Mean nearest-occluder distance (world units) over the occluded rays, 0 when fully lit (no occluder). The
	// SIGMA-style denoiser reads this from .a to size the à-trous kernel — near occluder (small) => sharp contact
	// shadow, far occluder (large) => wide soft penumbra. Noisy at few rays but spatially smooth + the denoiser
	// only uses it as a monotonic penumbra proxy.
	const float meanHitT = (hitCount > 0u) ? (hitTSum / float(hitCount)) : 0.0;
	ShadowOut[id.xy] = float4(D, meanHitT);                             // .rgb = colored shadowed diffuse irradiance, .a = penumbra guide
	ShadowSpecOut[id.xy] = float4(specVis, meanHitT); // colored specular VISIBILITY [0,1], .a = shared penumbra guide
}
