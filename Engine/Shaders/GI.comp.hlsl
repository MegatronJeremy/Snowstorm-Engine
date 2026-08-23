// Half-resolution ray-traced diffuse GI, compute stage (#124). This is the inline RayTraceGI from
// DefaultLit.frag.hlsl lifted into a standalone half-res pass over the depth+normal G-buffer. Per output
// pixel (at render.gi.scale of the viewport): reconstruct the receiver's world position from the G-buffer
// depth + InvViewProj, read its world normal, trace GI_RAY_COUNT cosine-weighted hemisphere rays against
// the bindless SceneTLAS, shade each committed hit as lit surface radiance (sun-with-shadow-ray + IBL
// ambient) via the geometry table, and average. Output is INCOMING IRRADIANCE only, NOT multiplied by
// the receiver albedo (that happens at full res in the forward pass, so the half-res GI never blurs
// albedo edges through the upsample). On a sky pixel (depth == far), output 0.
//
// Compiled only in the SS_RAYTRACING permutation (RayQuery). SceneTLAS + the bindless texture/cube arrays
// live in set 3 (gap-filled from VulkanBindlessManager by VulkanComputePipeline::Build), the SAME slots
// DefaultLit reads. This pass's own inputs (depth, normal, output UAV, sampler, params) are set 0.
//
// The hit-resolve/shade helpers (ResolveHit / ShadeSurfaceHit / geometry-table reads) now live in the
// shared Include/RTHitShading.hlsli (#129), included below and also used by the reflection compute pass:
// one implementation for both compute RT passes instead of drifting copies.

static const float PI = 3.14159265359;

// ---- Set 0: this pass's own resources ----
// One G-buffer color image carries BOTH the world normal (.xyz) and the NDC depth (.w); see
// DepthNormal.frag. Sampling one plain color image (not the depth-stencil attachment) sidesteps the
// DEPTH_STENCIL_READ_ONLY-vs-SHADER_READ_ONLY layout mismatch a compute sampled-image descriptor rejects.
Texture2D<float4> GBufferNormal : register(t0, space0); // .xy = oct GEOMETRIC normal, .z = roughness, .w = UNUSED (#129 Inc 1c)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u1, space0); // half-res irradiance (rgb)
SamplerState LinearSampler : register(s2, space0);       // bindless albedo / cubemap sampling
Texture2D<float> GBufferDepth : register(t4, space0);    // fp32 NDC depth (D32 attachment), sampled directly

cbuffer GICB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	float4x4 ViewProj;    // unused here but kept for parity/debug; cheap
	float3 CameraPosition;
	float GIRange;        // gather ray max distance (world units)

	uint2 OutSize;        // half-res dispatch dimensions
	float GIIntensity;    // scales the indirect contribution
	uint FrameCounter;    // per-frame sample rotation (TAA converges the few rays)

	// Sun (DirectionalLights[0]) for the one-bounce hit shading.
	float3 SunDirection;  // world-space light direction (points FROM light)
	float SunIntensity;
	float3 SunColor;
	float ShadowStrength; // lerp(1, visibility, ShadowStrength), matches the raster dial

	// IBL + geometry table.
	uint IrradianceCubeIndex;  // bindless cube index for hit ambient (0 = flat fill)
	uint PrefilteredCubeIndex; // bindless cube index for sky-miss bounce (0 = black)
	float IBLIntensity;
	uint LightCount;           // 0 = no sun

	uint ReflGeoTableAddrLo; // device address of the GeometryRecord table (lo/hi)
	uint ReflGeoTableAddrHi;
	uint RayCount;           // hemisphere-gather rays per pixel this frame (render.gi.rays, clamped [1,16])
	float GIBounceAmbient;   // #39: scale on the un-occluded IBL ambient at GI secondary hits (render.gi.bounce_ambient)

	// RTHitShading.hlsli's local-light contract. 16 = Snowstorm::kRTHitMaxLights (RTHitLights.hpp), which also
	// owns the packing; HitLightCount is 0 whenever render.rt.hit_lights is off.
	uint HitLightCount;
	uint3 _PadHitLights;
	float4 HitLightPosRange[16];
	float4 HitLightColor[16];
	float4 HitLightDirCos[16];
};

// Set 3 bindless (Textures/Cubemaps/SceneTLAS) + the geometry-table read + one-bounce hit shading
// (ResolveHit / ShadeSurfaceHit / RTHitShadowRay) are shared with the reflection compute pass (#129).
// See RTHitShading.hlsli's contract for the CB scalars it expects (SunDirection/Color/Intensity,
// LightCount, ShadowStrength, IrradianceCubeIndex, IBLIntensity; all provided by GICB above) and the
// LinearSampler on set 0. GeoTableAddress stays here (reassembles THIS CB's ReflGeoTableAddr lo/hi halves).
#define RTHIT_LOCAL_LIGHTS // the CB above carries HitLightCount + the three packed arrays
#include "Include/RTHitShading.hlsli"
#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

uint64_t GeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	// Half-res pixel center -> UV. The G-buffer is full-res; a linear depth/normal fetch here is the
	// nearest full-res texel to this half-res center (bilinear on depth would blend across silhouettes, so
	// use a point-ish load via the texel at the mapped full-res coord). We sample by UV with the linear
	// sampler for simplicity; the bilateral upsample (Inc 3) is where edge-correctness is enforced.
	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// POINT-fetch the full-res G-buffer at the nearest texel to this half-res pixel's center - never bilinear.
	// A linear tap blends depth across silhouettes (midpoint depth -> a reconstructed world position in mid-air
	// -> a garbage GI sample that bleeds a pixel past the edge). This was the "edge bleeding" (#129 Inc 2c); the
	// old code sampled linear "for simplicity" and left edge-correctness to the upsample, but the leak is in the
	// TRACE. Map the half-res UV to the full-res texel via the G-buffer's own dimensions.
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float4 gbuf = GBufferNormal.Load(int3(gbTexel, 0));
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r; // fp32 depth from the D32 attachment (was gbuf.w)
	// Sky / no geometry: the prepass clears depth to 1.0 and a real far-plane fragment is also ~1.0, so
	// depth >= 1 means "nothing here" (#129 Inc 1b: the old zero-normal test is invalid now that .xy is an
	// octahedral encoding where (0,0) is a valid normal).
	if (IsSky(depth))
	{
		GIOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as Sky.frag: NDC xy in [-1,1],
	// z = raw depth, w = 1, then perspective divide). NDC.y is NOT flipped, matching the sky reconstruction.
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	const float3 N = DecodeNormalOct(gbuf.xy); // .xy = octahedral world normal (#129 Inc 1b)
	const float3 Ng = N; // reuse the shading normal for the ray offset

	const uint64_t tableAddr = GeoTableAddress();

	// Orthonormal basis (tangent, bitangent, N) to orient the cosine hemisphere.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	// Per-pixel + per-frame interleaved-gradient-noise rotation seed. Use the full-res pixel coord so the
	// hash is stable in screen space across scale changes (id.xy is half-res; scale by the ratio).
	const float2 px = float2(id.xy) + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + Ng * 0.02;

	// Runtime ray count (render.gi.rays): dynamic loop bound, so [loop] not [unroll]. Guaranteed >= 1 by the
	// C++ clamp, so the /rayCount normalization below never divides by zero.
	const uint rayCount = max(RayCount, 1u);
	float3 incoming = float3(0, 0, 0);
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
		ray.TMax = GIRange;

		// Alpha-test cutout occluders (masked instances are FORCE_NON_OPAQUE, surfacing as candidates); opaque
		// hits auto-commit so the loop body only runs for masked geometry.
		RayQuery<RAY_FLAG_NONE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		while (q.Proceed())
		{
			if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
			    RTCommitCandidate(tableAddr, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), LinearSampler))
			{
				q.CommitNonOpaqueTriangleHit();
			}
		}

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT && tableAddr != 0)
		{
			const float3 hitPos = origin + dir * q.CommittedRayT();
			incoming += ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos, GIBounceAmbient);
		}
		else if (PrefilteredCubeIndex != 0)
		{
			incoming += Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb * IBLIntensity;
		}
	}

	// Incoming irradiance, intensity-scaled. NO receiver albedo: that's multiplied at full res in the
	// forward pass after the bilateral upsample, so half-res GI never blurs albedo edges. GIIntensity is a
	// linear scalar (no effect on edges), applied here so the debug view shows the tuned signal.
	const float3 irradiance = (incoming / float(rayCount)) * GIIntensity;
	GIOut[id.xy] = float4(irradiance, 1.0);
}
