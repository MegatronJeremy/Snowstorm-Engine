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

	// Slim tracer + importance-weight params (16-byte rows):
	float4 DirData[SHADOW_MAX_DIR];         // xyz = normalized dir TO light, w = luma(color*intensity)
	float4 PointPosRange[SHADOW_MAX_POINT]; // xyz = world pos, w = range
	float4 PointLum[SHADOW_MAX_POINT];      // x = luma(color*intensity)
	float4 SpotPosRange[SHADOW_MAX_SPOT];   // xyz = world pos, w = range
	float4 SpotDirCos[SHADOW_MAX_SPOT];     // xyz = spot forward axis, w = cos(outer half-angle)
	float4 SpotLumInner[SHADOW_MAX_SPOT];   // x = luma(color*intensity), y = cos(inner half-angle)
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky

// Cheap per-(pixel,frame,light) hash in [0,1) for the weighted-reservoir random stream. Interleaved-gradient
// style, decorrelated per light index by a large stride — same family the AO/GI/soft-shadow passes use.
float Rand01(uint2 px, uint frame, uint lightIdx)
{
	const float2 p = float2(px) + float2((frame * 3u + lightIdx * 7u) * 5.588238, (frame * 5u + lightIdx * 11u) * 3.539418);
	return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

// Windowed inverse-square attenuation, matching DefaultLit's point/spot falloff exactly.
float FalloffWindow(float dist, float range)
{
	const float t = saturate(1.0 - pow(dist / range, 4.0));
	return (t * t) / max(dist * dist, 1e-4);
}

// One-pass weighted reservoir selection over ALL in-range lights, weight = unshadowed contribution. `seed`
// decorrelates the random stream per sample (and frame), so calling this K times draws K independent lights
// ~proportional to contribution -> averaging their visibility is a K-sample estimate of the aggregate shadow
// ratio (variance ~1/K). Returns false when no light contributes here; else fills the chosen light's tracer
// params (direction TO light, ray tMax, soft cone/disk radius, whether it casts a shadow).
bool SelectLight(uint2 px, float3 positionWS, float3 N, uint seed, out float3 outL, out float outTMax,
                 out float outConeR, out bool outCasts)
{
	float wSum = 0.0;
	outL = float3(0, 0, 1);
	outTMax = 1e30;
	outConeR = 0.0;
	outCasts = false;
	bool have = false;
	uint lightIdx = 0; // global stream index, for decorrelated randoms

	// Directional (sun and any extra suns): infinite distance, no attenuation.
	for (uint d = 0; d < DirCount; ++d, ++lightIdx)
	{
		const float3 L = DirData[d].xyz;
		const float w = DirData[d].w * max(dot(N, L), 0.0);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (Rand01(px, seed, lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = 1e30;
			outConeR = SunTanAngular; // sun angular half-size
			outCasts = (DirCastMask & (1u << d)) != 0u;
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
		const float w = PointLum[p].x * FalloffWindow(dist, range) * max(dot(N, L), 0.0);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (Rand01(px, seed, lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeR = SourceRadius / max(dist, 1e-4); // source disk subtends a wider cone up close
			outCasts = (PointCastMask & (1u << p)) != 0u;
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
		const float denom = max(SpotLumInner[s].y - cosOuter, 1e-4);
		const float cone = pow(saturate((cosAngle - cosOuter) / denom), 2.0);
		const float w = SpotLumInner[s].x * FalloffWindow(dist, range) * cone * max(dot(N, L), 0.0);
		if (w <= 0.0)
		{
			continue;
		}
		wSum += w;
		if (Rand01(px, seed, lightIdx) < w / wSum)
		{
			outL = L;
			outTMax = max(dist - 0.05, 0.0);
			outConeR = SourceRadius / max(dist, 1e-4);
			outCasts = (SpotCastMask & (1u << s)) != 0u;
			have = true;
		}
	}

	return have;
}

// Trace one (optionally area-jittered) shadow ray toward the chosen light. Returns visibility (1 = lit).
float TraceShadow(uint2 px, uint seed, float3 positionWS, float3 N, float3 L, float tMax, float coneR)
{
	float3 dir = L;
	// Soft shadows: jitter the ray within the light's area (disk of radius coneR perpendicular to L). The
	// per-sample + temporal + à-trous averaging converges the penumbra (MegaLights/RTXDI area-light approach).
	if (SoftEnabled != 0u && coneR > 0.0)
	{
		const float3 up = abs(L.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
		const float3 tangent = normalize(cross(up, L));
		const float3 bitangent = cross(L, tangent);
		const float u1 = Rand01(px, seed, 4096u);
		const float u2 = Rand01(px, seed, 8192u);
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
	return (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
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

	// Sky / no geometry -> fully lit (ratio 1). The forward's unshadowed term is ~0 here anyway.
	if (IsSky(depth))
	{
		ShadowOut[id.xy] = 1.0;
		return;
	}

	const float2 ndc = uv * 2.0 - 1.0;
	const float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;
	const float3 N = DecodeNormalOct(gbuf.xy);

	// --- K-sample estimate of the aggregate shadow ratio (render.shadows.rays). Each sample independently
	// importance-samples one light (∝ unshadowed contribution) and traces one ray; averaging cuts the binary
	// 1-sample variance ~1/K, so the temporal + à-trous converge cleaner (fixes the edge/motion noise). A
	// non-casting or no-light sample contributes visibility 1 (lit); only a sampled caster traces.
	const uint rayCount = max(RayCount, 1u);
	float visSum = 0.0;
	[loop] for (uint s = 0; s < rayCount; ++s)
	{
		const uint seed = FrameCounter * 16u + s; // decorrelate per sample AND per frame
		float3 L;
		float tMax;
		float coneR;
		bool casts;
		if (!SelectLight(id.xy, positionWS, N, seed, L, tMax, coneR, casts) || !casts)
		{
			visSum += 1.0; // no contributing light / non-casting chosen -> lit
			continue;
		}
		visSum += TraceShadow(id.xy, seed, positionWS, N, L, tMax, coneR);
	}

	const float ratio = visSum / float(rayCount);
	ShadowOut[id.xy] = float4(ratio, ratio, ratio, 1.0); // ShadowStrength applied in DefaultLit at consumption
}
