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

#define AO_RAY_COUNT 2

// ---- Set 0: this pass's own resources ----
// One G-buffer color image carries BOTH the world normal (.xyz) and the NDC depth (.w) — see
// DepthNormal.frag. Sampling one plain color image (not the depth-stencil attachment) sidesteps the
// DEPTH_STENCIL_READ_ONLY-vs-SHADER_READ_ONLY layout mismatch a compute sampled-image descriptor rejects.
Texture2D<float4> GBufferNormal : register(t0, space0);                              // .xyz = world normal, .w = NDC depth
// Occlusion factor in .r (the RHI has no single-channel float format; RGBA16F matches GITarget — a half-res
// target, so the 4x-vs-R16 memory is negligible). The upsample + forward read only .r.
[[vk::image_format("rgba16f")]] RWTexture2D<float4> AOOut : register(u1, space0);    // half-res occlusion factor [0,1] in .r
SamplerState LinearSampler : register(s2, space0);

cbuffer AOCB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	uint2 OutSize;        // half-res dispatch dimensions
	float AORadius;       // occlusion ray max distance (world units)
	float AOIntensity;    // scales the darkening (1 = physical, >1 = artistic boost)
	uint FrameCounter;    // per-frame sample rotation (TAA converges the few rays)
	uint3 _Pad;
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	const float4 gbuf = GBufferNormal.SampleLevel(LinearSampler, uv, 0);
	const float depth = gbuf.w; // NDC depth packed into .w by the prepass
	// Sky / no geometry: a zero-length normal means the pixel is sky (prepass cleared the color target to 0);
	// also treat depth >= 1 (far plane) as sky. No surface -> fully open (AO = 1).
	if (dot(gbuf.xyz, gbuf.xyz) < 1e-6 || depth >= 1.0)
	{
		AOOut[id.xy] = 1.0;
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as GI.comp / Sky.frag).
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	const float3 N = normalize(gbuf.xyz);
	const float3 Ng = N; // the prepass stores the geometric-ish vertex normal; reuse for the ray offset

	// Orthonormal basis (tangent, bitangent, N) to orient the cosine hemisphere.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	// Per-pixel + per-frame interleaved-gradient-noise rotation seed (same hash GI/RTAO use).
	const float2 px = float2(id.xy) + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02;

	float occlusion = 0.0;
	[unroll] for (int s = 0; s < AO_RAY_COUNT; ++s)
	{
		const float u1 = frac((float(s) + ign) / float(AO_RAY_COUNT));
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
			occlusion += 1.0 - saturate(q.CommittedRayT() / AORadius);
		}
	}

	// Occlusion factor in [0,1] (1 = fully open), pre-scaled by AOIntensity. Averaged over the rays; the
	// forward pass multiplies this into `ao` at full res after the bilateral upsample.
	occlusion = (occlusion / float(AO_RAY_COUNT)) * AOIntensity;
	AOOut[id.xy] = saturate(1.0 - occlusion);
}
