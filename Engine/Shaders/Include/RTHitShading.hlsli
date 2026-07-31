// RTHitShading.hlsli — shared inline-RayQuery hit resolution + one-bounce shading for the COMPUTE RT
// passes (GI #124, Reflection #129). Both trace the bindless SceneTLAS, resolve a committed triangle hit
// to its surface via the per-instance geometry table (device-address vertex/index reads + barycentric UV
// + bindless albedo), and re-light it cheaply (sun-with-shadow-ray + IBL/flat ambient). Extracted from the
// TEMPORARY copy that lived in GI.comp.hlsl (the #124 note foreshadowed this) so the two compute passes
// share ONE implementation instead of drifting copies.
//
// NOTE: this is the COMPUTE flavour — the sun comes from the includer's OWN constant buffer as scalar
// fields (SunDirection/SunColor/SunIntensity), NOT the DirectionalLights[] material-set array a fragment
// shader reads. DefaultLit.frag keeps its own array-based ShadeSurfaceHit; it does not include this.
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

// One reflection geometry record, matching GeometryRecord (ReflectionGeometrySingleton.hpp) byte-for-byte
// (dx layout, 112-byte stride). Read field-by-field via vk::RawBufferLoad off the record's base address.
struct GeoRecord
{
	uint64_t VertexAddress;
	uint64_t IndexAddress;
	uint AlbedoTextureIndex;
	float4 BaseColor;
	float4x4 Model;
};

GeoRecord LoadGeoRecord(uint64_t tableAddr, uint instanceIndex)
{
	const uint64_t base = tableAddr + uint64_t(instanceIndex) * 112ull;
	GeoRecord r;
	r.VertexAddress = vk::RawBufferLoad<uint64_t>(base + 0, 8);
	r.IndexAddress = vk::RawBufferLoad<uint64_t>(base + 8, 8);
	r.AlbedoTextureIndex = vk::RawBufferLoad<uint>(base + 16, 4);
	r.BaseColor = float4(vk::RawBufferLoad<float>(base + 32, 4), vk::RawBufferLoad<float>(base + 36, 4),
	                     vk::RawBufferLoad<float>(base + 40, 4), vk::RawBufferLoad<float>(base + 44, 4));
	// mat4 is 16 contiguous floats at offset 48 (column-major, matching glm).
	float4 c0 = float4(vk::RawBufferLoad<float>(base + 48, 4), vk::RawBufferLoad<float>(base + 52, 4),
	                   vk::RawBufferLoad<float>(base + 56, 4), vk::RawBufferLoad<float>(base + 60, 4));
	float4 c1 = float4(vk::RawBufferLoad<float>(base + 64, 4), vk::RawBufferLoad<float>(base + 68, 4),
	                   vk::RawBufferLoad<float>(base + 72, 4), vk::RawBufferLoad<float>(base + 76, 4));
	float4 c2 = float4(vk::RawBufferLoad<float>(base + 80, 4), vk::RawBufferLoad<float>(base + 84, 4),
	                   vk::RawBufferLoad<float>(base + 88, 4), vk::RawBufferLoad<float>(base + 92, 4));
	float4 c3 = float4(vk::RawBufferLoad<float>(base + 96, 4), vk::RawBufferLoad<float>(base + 100, 4),
	                   vk::RawBufferLoad<float>(base + 104, 4), vk::RawBufferLoad<float>(base + 108, 4));
	r.Model = float4x4(c0, c1, c2, c3);
	return r;
}

// Read a mesh vertex's TexCoord (float2 @ offset 24 in the 48-byte Vertex) by device address.
float2 LoadVertexUV(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 24ull;
	return float2(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4));
}

// Read a mesh vertex's object-space Normal (float3 @ offset 12) by device address.
float3 LoadVertexNormal(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 12ull;
	return float3(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4));
}

struct HitSurface
{
	float3 Albedo;
	float3 Nw; // interpolated world normal
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
	// Interpolated object normal -> world via the record's Model (rows hold glm's columns, so
	// mul(n, Model3x3) computes glmModel * n). Ignores non-uniform scale (inverse-transpose) — fine here.
	const float3 nObj = w * LoadVertexNormal(rec.VertexAddress, i0) + bary.x * LoadVertexNormal(rec.VertexAddress, i1) + bary.y * LoadVertexNormal(rec.VertexAddress, i2);
	s.Nw = normalize(mul(nObj, (float3x3)rec.Model));
	return s;
}

// Shadow ray for the one-bounce hit shading (ACCEPT_FIRST_HIT: occlusion only).
float RTHitShadowRay(float3 positionWS, float3 Ng, float3 L, float tMax)
{
	const float3 origin = positionWS + Ng * 0.02 + L * 0.01;
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = L;
	ray.TMin = 0.0;
	ray.TMax = tMax;
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();
	const float visibility = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
	return lerp(1.0, visibility, ShadowStrength);
}

// Shade a committed hit as LIT surface radiance: resolve it then re-light cheaply — sun (from the
// includer's CB) with a shadow ray + an IBL/flat ambient fill (so a hit on a shadowed surface still
// contributes its ambient, not black). ONE bounce: the shaded hit does NOT itself trace. `hitPos` =
// world hit position (caller: rayOrigin + rayDir * CommittedRayT).
float3 ShadeSurfaceHit(uint64_t tableAddr, uint instanceId, uint prim, float2 bary, float3 hitPos)
{
	const HitSurface s = ResolveHit(tableAddr, instanceId, prim, bary);

	float3 direct = float3(0, 0, 0);
	if (LightCount > 0)
	{
		const float3 Lsun = normalize(-SunDirection);
		const float ndl = saturate(dot(s.Nw, Lsun));
		if (ndl > 0.0)
		{
			const float sh = RTHitShadowRay(hitPos, s.Nw, Lsun, 1e30);
			direct = SunColor * SunIntensity * ndl * sh;
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

	return s.Albedo * (direct + ambient);
}

#endif // SNOWSTORM_RT_HIT_SHADING_HLSLI
