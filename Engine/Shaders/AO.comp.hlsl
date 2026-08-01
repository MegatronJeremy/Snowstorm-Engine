// Half-resolution ray-traced ambient occlusion, compute stage (#126). The inline RayTraceAO from
// DefaultLit.frag.hlsl lifted into a standalone half-res pass over the depth+normal G-buffer — a strict
// SUBSET of the GI compute pass (GI.comp.hlsl): AO is occupancy-only (ACCEPT_FIRST_HIT + distance
// falloff), so there's NO geometry table, NO hit shading, NO sun/IBL/cubemap params. Per output pixel
// (at render.ao.scale of the viewport): reconstruct world position from the G-buffer depth + InvViewProj,
// read the world normal, trace AO_RAY_COUNT short cosine-hemisphere occlusion rays, accumulate distance
// falloff, and write a single occlusion FACTOR in [0,1] (1 = fully open). On a sky pixel, output 1 (open).
//
// Compiled only in the SS_RAYTRACING permutation (RayQuery). SceneTLAS lives in set 3 (gap-filled from
// VulkanBindlessManager by VulkanComputePipeline::Build). This pass's own inputs (G-buffer, output UAV,
// sampler, params) are set 0. Mirrors GI.comp.hlsl's structure; see #124 for the pipeline rationale.

static const float PI = 3.14159265359;

// ---- Set 0: this pass's own resources ----
// One G-buffer color image carries BOTH the world normal (.xyz) and the NDC depth (.w) — see
// DepthNormal.frag. Sampling one plain color image (not the depth-stencil attachment) sidesteps the
// DEPTH_STENCIL_READ_ONLY-vs-SHADER_READ_ONLY layout mismatch a compute sampled-image descriptor rejects.
Texture2D<float4> GBufferNormal : register(t0, space0);                              // .xy = oct GEOMETRIC normal, .z = roughness, .w = UNUSED (#129 Inc 1c)
Texture2D<float> GBufferDepth : register(t4, space0);                                // fp32 NDC depth (D32 attachment), sampled directly
// Occlusion factor in .r (the RHI has no single-channel float format; RGBA16F matches GITarget — a half-res
// target, so the 4x-vs-R16 memory is negligible). The upsample + forward read only .r.
[[vk::image_format("rgba16f")]] RWTexture2D<float4> AOOut : register(u1, space0);    // half-res occlusion factor [0,1] in .r
// #129 Inc 2c: AO no longer declares a sampler — the G-buffer is now POINT-fetched via Load (no bilinear),
// and AO is occupancy-only (no hit shading, no bindless texture/cubemap sampling), so nothing needs one.

cbuffer AOCB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	uint2 OutSize;        // half-res dispatch dimensions
	float AORadius;       // occlusion ray max distance (world units)
	float AOIntensity;    // scales the darkening (1 = physical, >1 = artistic boost)
	uint FrameCounter;    // per-frame sample rotation (TAA converges the few rays)
	uint RayCount;        // occlusion rays per pixel this frame (render.ao.rays, clamped [1,16])
	uint2 _Pad;
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// POINT-fetch the full-res G-buffer at the nearest texel — never bilinear (a linear tap blends depth across
	// silhouettes -> midpoint depth -> world position in mid-air -> AO that bleeds past the edge). #129 Inc 2c.
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float4 gbuf = GBufferNormal.Load(int3(gbTexel, 0));
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r; // fp32 depth from the D32 attachment (was gbuf.w)
	// Sky / no geometry (prepass clears depth to 1.0; far plane also ~1.0) -> fully open (AO = 1). #129 Inc 1b:
	// depth-based, not zero-normal (oct(0,0) is a valid normal now).
	if (IsSky(depth))
	{
		AOOut[id.xy] = 1.0;
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as GI.comp / Sky.frag).
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	const float3 N = DecodeNormalOct(gbuf.xy); // .xy = octahedral world normal (#129 Inc 1b)
	const float3 Ng = N; // reuse the shading normal for the ray offset

	// Orthonormal basis (tangent, bitangent, N) to orient the cosine hemisphere.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	// Per-pixel + per-frame interleaved-gradient-noise rotation seed (same hash GI/RTAO use).
	const float2 px = float2(id.xy) + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02;

	float occlusion = 0.0;
	// #130 Inc B: accumulate mean occluder hit distance for the denoiser's hit-distance-guided à-trous. A miss
	// contributes AORadius (the max), so a fully-open pixel -> meanHitT == AORadius -> normalized .a == 1. This
	// lets the à-trous keep near contact-shadow gradients (small .a) sharp while blurring distant AO wider,
	// the NRD REBLUR trick — the signal is discarded when render.ao.denoise.hitdist == 0.
	float hitTSum = 0.0;
	// Runtime ray count (render.ao.rays): dynamic loop bound, so [loop] not [unroll]. >= 1 by the C++ clamp.
	const uint rayCount = max(RayCount, 1u);
	[loop] for (uint s = 0; s < rayCount; ++s)
	{
		const float u1 = frac((float(s) + ign) / float(rayCount));
		const float u2 = frac(ign + float(s) * 0.61803398875);
		const float r = sqrt(u1);
		const float phi = 2.0 * PI * u2;
		const float3 localDir = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
		const float3 dir = normalize(localDir.x * tangent + localDir.y * bitangent + localDir.z * N);

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = dir;
		ray.TMin = 0.0;
		ray.TMax = AORadius;

		// Occupancy only: ACCEPT_FIRST_HIT_AND_END_SEARCH — AO just needs "is anything within AORadius".
		RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		q.Proceed();

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// Distance falloff: a near hit occludes more than a far one.
			const float t = q.CommittedRayT();
			occlusion += 1.0 - saturate(t / AORadius);
			hitTSum += t;
		}
		else
		{
			hitTSum += AORadius; // a miss = no occluder within range = the far end of the guidance signal
		}
	}

	// Occlusion factor in [0,1] (1 = fully open), pre-scaled by AOIntensity. Averaged over the rays; the
	// forward pass multiplies this into `ao` at full res after the bilateral upsample. The mean hit distance
	// (normalized to [0,1]) rides .a for the denoiser; the color path derives luma from .rgb so .a is free.
	occlusion = (occlusion / float(rayCount)) * AOIntensity;
	const float ao = saturate(1.0 - occlusion);
	const float meanHitT = saturate((hitTSum / float(rayCount)) / AORadius);
	AOOut[id.xy] = float4(ao, ao, ao, meanHitT);
}
