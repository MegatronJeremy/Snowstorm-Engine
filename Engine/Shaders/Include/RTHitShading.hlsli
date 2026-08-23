// RTHitShading.hlsli — shared inline-RayQuery hit resolution + one-bounce shading for the COMPUTE RT
// passes (GI #124, Reflection #129). Both trace the bindless SceneTLAS, resolve a committed triangle hit
// to its surface via the per-instance geometry table (device-address vertex/index reads + barycentric UV
// + bindless albedo), and re-light it cheaply (sun-with-shadow-ray + IBL/flat ambient). Extracted from the
// TEMPORARY copy that lived in GI.comp.hlsl (the #124 note foreshadowed this) so the two compute passes
// share ONE implementation instead of drifting copies.
//
// NOTE: this is the COMPUTE flavour: the sun comes from the includer's OWN constant buffer as scalar
// fields (SunDirection/SunColor/SunIntensity), NOT the DirectionalLights[] material-set array a fragment
// shader reads. That is why DefaultLit.frag does not include this file.
//
// CONTRACT — the includer MUST declare, BEFORE #include-ing this file:
//   * set 3 bindless is declared HERE (Textures/Cubemaps/SceneTLAS) — identical in every compute RT pass,
//     gap-filled by the compute pipeline builder. Do NOT re-declare it in the includer.
//   * a clamp/wrap sampler named `LinearSampler` (the includer owns it on set 0).
//   * these constant-buffer scalars (any cbuffer, any binding — referenced by name):
//       uint  LightCount;          // 0 = no sun
//       float3 SunDirection;       // world-space light direction (points FROM the light)
//       float3 SunColor;  float SunIntensity;
//       float ShadowStrength;      // lerp(1, visibility, ShadowStrength)
//       uint  IrradianceCubeIndex; // bindless cube for hit ambient (0 = flat 0.03 fill)
//       float IBLIntensity;
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

struct HitSurface
{
	float3 Albedo;
	float3 Nw;      // interpolated world normal
	float Metallic; // diffuse response is scaled by (1 - Metallic); this pass models no specular lobe
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
	// Interpolated object normal -> world via the record's Model (rows hold glm's columns, so
	// mul(n, Model3x3) computes glmModel * n). Ignores non-uniform scale (inverse-transpose) — fine here.
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

// Shade a committed hit as LIT surface radiance: resolve it then re-light cheaply — sun (from the
// includer's CB) with a shadow ray + an IBL/flat ambient fill (so a hit on a shadowed surface still
// contributes its ambient, not black). ONE bounce: the shaded hit does NOT itself trace. `hitPos` =
// world hit position (caller: rayOrigin + rayDir * CommittedRayT).
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
	return s.Albedo * (1.0 - s.Metallic) * (direct + ambient * ambientScale);
}

#endif // SNOWSTORM_RT_HIT_SHADING_HLSLI
