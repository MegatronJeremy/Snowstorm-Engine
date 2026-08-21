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
Texture2D<float4> GBufferNormal : register(t0, space0); // .xy = oct GEOMETRIC normal
Texture2D<float> GBufferDepth : register(t4, space0);   // fp32 NDC depth (D32 attachment)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ShadowOut : register(u1, space0); // aggregate shadow ratio in .r

cbuffer ShadowCB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	uint2 OutSize;        // half-res dispatch dimensions
	float NormalBias;     // world-space normal offset for the ray origin (acne/peter-pan guard)
	uint FrameCounter;    // per-frame sample rotation (the temporal pass converges the 1 ray/pixel)

	uint DirCount;   // active directional lights (<= SHADOW_MAX_DIR)
	uint PointCount; // active point lights (<= SHADOW_MAX_POINT)
	uint SpotCount;  // active spot lights (<= SHADOW_MAX_SPOT)
	uint _Pad0;

	uint DirCastMask;   // bit i set => dir light i casts a shadow (else vis=1, no ray)
	uint PointCastMask; // bit i => point light i casts
	uint SpotCastMask;  // bit i => spot light i casts
	uint SoftEnabled;   // 1 => jitter the chosen ray within the light's area (soft penumbra); 0 => hard ray

	float SunTanAngular; // tan(sun angular half-size) -> directional cone radius for the soft jitter
	float SourceRadius;  // local-light source radius (world units); spot/point cone radius = SourceRadius / dist
	uint RayCount;       // stochastic samples/pixel (render.shadows.rays): more = less variance, ~linear cost
	float _Pad3;

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

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky

// Interleaved-gradient noise in [0,1), FIXED per pixel — its power spectrum is blue-ish in screen space
// (Jimenez), so neighbouring pixels get well-separated values that the spatial (à-trous) denoiser averages
// cleanly. This is the spatial base for the spatiotemporal blue-noise sampler below.
float IGN(uint2 px)
{
	return frac(52.9829189 * frac(0.06711056 * float(px.x) + 0.00583715 * float(px.y)));
}

// Spatiotemporal blue-noise sample in [0,1): the fixed IGN spatial pattern advanced by a golden-ratio (R1
// low-discrepancy) increment per FRAME and decorrelated per DIMENSION. Animating blue noise by the golden
// ratio keeps the screen-space blue-noise property while making successive frames low-discrepancy IN TIME
// (Wolfe, "Animating Noise for Integration Over Time"; MegaLights/NRD both stress blue + low-discrepancy at
// 1-4 rpp) — far better temporal convergence and far less shimmer than the previous white-noise-in-time hash,
// which re-rolled the whole pattern every frame. `dim` separates the per-frame draws (each light in the
// reservoir stream + the two soft-jitter axes). 0.6180339887 = 1/φ (temporal); 0.7548776662 = the plastic-
// number R2 additive constant (per-dimension decorrelation).
float STBN(uint2 px, uint frame, uint dim)
{
	// Wrap the frame to a 64-frame period: keeps float(frame) exact over arbitrarily long runs AND matches NRD's
	// "limited animated frames" guidance for blue noise. dim is small (<= ~50) so it stays exact too.
	return frac(IGN(px) + float(frame & 63u) * 0.61803398875 + float(dim) * 0.75487766624);
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
// TO light, ray tMax, soft cone/disk radius, whether it casts a shadow).
bool SelectLight(uint2 px, float3 positionWS, float3 N, uint frame, uint dimBase, out float3 outL, out float outTMax,
                 out float outConeR, out bool outCasts, out float3 outRadNdotL, out float outWSum)
{
	float wSum = 0.0;
	outL = float3(0, 0, 1);
	outTMax = 1e30;
	outConeR = 0.0;
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
		const float w = Luma3(radNdotL); // importance weight = luma of the colored contribution
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = 1e30;
			outConeR = SunTanAngular; // sun angular half-size
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
		const float w = Luma3(radNdotL);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeR = SourceRadius / max(dist, 1e-4); // source disk subtends a wider cone up close
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
		const float w = Luma3(radNdotL);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (STBN(px, frame, dimBase + lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeR = SourceRadius / max(dist, 1e-4);
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
float TraceShadow(uint2 px, uint frame, uint dimBase, float3 positionWS, float3 N, float3 L, float tMax, float coneR, out float outHitT)
{
	float3 dir = L;
	// Soft shadows: jitter the ray within the light's area (disk of radius coneR perpendicular to L). The
	// per-sample + temporal + à-trous averaging converges the penumbra (MegaLights/RTXDI area-light approach).
	if (SoftEnabled != 0u && coneR > 0.0)
	{
		const float3 up = abs(L.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
		const float3 tangent = normalize(cross(up, L));
		const float3 bitangent = cross(L, tangent);
		// Jitter axes at high dims (past the light-stream dims for this sample) so they don't collide.
		const float u1 = STBN(px, frame, dimBase + 48u);
		const float u2 = STBN(px, frame, dimBase + 49u);
		const float rr = coneR * sqrt(u1);
		const float phi = 6.2831853 * u2;
		dir = normalize(L + (rr * cos(phi)) * tangent + (rr * sin(phi)) * bitangent);
	}

	RayDesc ray;
	ray.Origin = positionWS + N * NormalBias + L * 0.01;
	ray.Direction = dir;
	ray.TMin = 0.0;
	ray.TMax = tMax;

	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();
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
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r;

	// Sky / no geometry -> no direct irradiance (0). DefaultLit doesn't shade sky pixels (the sky pass does),
	// so this only keeps the denoiser/upsample from bleeding a bogus value in from the background.
	if (IsSky(depth))
	{
		ShadowOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	const float2 ndc = uv * 2.0 - 1.0;
	const float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;
	const float3 N = DecodeNormalOct(gbuf.xy);

	// --- K-sample RIS estimate of the COLORED shadowed direct irradiance D = Σ_i radiance_i·NdotL_i·vis_i
	// (Option B). Each sample importance-samples one light y ∝ luma-contribution and traces one ray; the RIS
	// estimate of the whole sum is radiance_y·NdotL_y·vis_y · wSum / contrib_y (contrib_y = luma of that colored
	// term). Averaging K samples converges to D. The forward multiplies full-res albedo/π·(1-metallic) — albedo
	// factors out of the diffuse sum, so it stays out of this half/full-res buffer (no albedo blur), the GI
	// pattern. A non-casting chosen light contributes with vis = 1 (lit); no-light -> 0.
	const uint rayCount = max(RayCount, 1u);
	float3 Dsum = float3(0, 0, 0);
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
		float coneR;
		bool casts;
		float3 radNdotL; // chosen light's colored radiance*NdotL (pre-visibility)
		float wSum;      // Σ luma-contribution over all in-range lights (the RIS normalization)
		if (!SelectLight(id.xy, positionWS, N, FrameCounter, dimBase, L, tMax, coneR, casts, radNdotL, wSum))
		{
			continue; // no contributing light here -> 0 irradiance from this sample
		}
		float vis = 1.0; // non-casting chosen light is fully lit (no ray)
		if (casts)
		{
			float hitT;
			vis = TraceShadow(id.xy, FrameCounter, dimBase, positionWS, N, L, tMax, coneR, hitT);
			if (hitT >= 0.0)
			{
				hitTSum += hitT;
				++hitCount;
			}
		}
		// RIS: estimate of the FULL colored sum from this one sample = radNdotL·vis · wSum / contrib_y.
		const float contribY = Luma3(radNdotL);
		if (contribY > 1e-6)
		{
			Dsum += radNdotL * (vis * wSum / contribY);
		}
	}

	const float3 D = Dsum / float(rayCount); // colored shadowed direct irradiance (no albedo)
	// Mean nearest-occluder distance (world units) over the occluded rays, 0 when fully lit (no occluder). The
	// SIGMA-style denoiser reads this from .a to size the à-trous kernel — near occluder (small) => sharp contact
	// shadow, far occluder (large) => wide soft penumbra. Noisy at few rays but spatially smooth + the denoiser
	// only uses it as a monotonic penumbra proxy.
	const float meanHitT = (hitCount > 0u) ? (hitTSum / float(hitCount)) : 0.0;
	ShadowOut[id.xy] = float4(D, meanHitT); // .rgb = colored shadowed irradiance, .a = penumbra guide; forward applies albedo + ShadowStrength
}
