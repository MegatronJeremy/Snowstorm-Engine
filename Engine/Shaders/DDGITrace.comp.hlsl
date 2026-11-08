// DDGI probe trace: one thread per (ray, probe). Each ray writes the radiance it found and its hit distance into
// the ray buffer (row = probe, column = ray). A hit is shaded with its direct light plus the irradiance the probes
// held last frame, so every frame adds a bounce. A backface hit means the probe sits inside geometry: no radiance,
// and a negative distance the blends read as such.

#include "Include/DDGICommon.hlsli"

SamplerState LinearSampler : register(s1, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> RayOut : register(u2, space0);

#define RTHIT_LOCAL_LIGHTS
#include "Include/RTHitShading.hlsli"

static const float kBackfaceDistanceScale = -0.2;

[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const DDGIVolume volume = CurrentVolume();
	const uint ray = id.x;
	const uint probe = id.y;
	if (ray >= RaysPerProbe || probe >= DDGIProbeCount(volume))
	{
		return;
	}

	const uint64_t tableAddr = (uint64_t(GeoTableAddrHi) << 32) | uint64_t(GeoTableAddrLo);
	const float3 origin = DDGIProbePosition(volume, DDGIProbeCoords(volume, probe));
	const float3 dir = RayDirection(ray);

	RayDesc desc;
	desc.Origin = origin;
	desc.Direction = dir;
	desc.TMin = 0.0;
	desc.TMax = MaxRayDistance;
	RayQuery<RAY_FLAG_NONE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, desc);
	while (q.Proceed())
	{
		if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
		    RTCommitCandidate(tableAddr, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), LinearSampler))
		{
			q.CommitNonOpaqueTriangleHit();
		}
	}

	if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || tableAddr == 0)
	{
		float3 sky = float3(0, 0, 0);
		if (PrefilteredCubeIndex != 0)
		{
			sky = Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb * IBLIntensity;
		}
		RayOut[uint2(ray, probe)] = float4(sky, MaxRayDistance);
		return;
	}

	const float t = q.CommittedRayT();
	const HitSurface s = ResolveHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
	if (dot(s.Nw, dir) > 0.0)
	{
		RayOut[uint2(ray, probe)] = float4(0.0, 0.0, 0.0, kBackfaceDistanceScale * t);
		return;
	}

	const float3 hitPos = origin + dir * t;
	float3 indirect = float3(0, 0, 0);
	if (Reset == 0)
	{
		const float2 irradianceSize = float2(IrradianceAtlasSize);
		const float2 distanceSize = float2(DistanceAtlasSize);
		indirect = DDGIIrradiance(volume, Textures[NonUniformResourceIndex(IrradianceAtlasIndex)], Textures[NonUniformResourceIndex(DistanceAtlasIndex)],
		                          LinearSampler, irradianceSize, distanceSize, hitPos, s.Nw, -dir);
	}
	RayOut[uint2(ray, probe)] = float4(ShadeResolvedHit(tableAddr, s, hitPos, indirect), t);
}
