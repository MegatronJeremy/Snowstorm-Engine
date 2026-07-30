// Half-resolution ray-traced diffuse GI, compute stage (#124). This is the inline RayTraceGI from
// DefaultLit.frag.hlsl lifted into a standalone half-res pass over the depth+normal G-buffer. Per output
// pixel (at render.gi.scale of the viewport): reconstruct the receiver's world position from the G-buffer
// depth + InvViewProj, read its world normal, trace GI_RAY_COUNT cosine-weighted hemisphere rays against
// the bindless SceneTLAS, shade each committed hit as lit surface radiance (sun-with-shadow-ray + IBL
// ambient) via the geometry table, and average. Output is INCOMING IRRADIANCE only — NOT multiplied by
// the receiver albedo (that happens at full res in the forward pass, so the half-res GI never blurs
// albedo edges through the upsample). On a sky pixel (depth == far), output 0.
//
// Compiled only in the SS_RAYTRACING permutation (RayQuery). SceneTLAS + the bindless texture/cube arrays
// live in set 3 (gap-filled from VulkanBindlessManager by VulkanComputePipeline::Build), the SAME slots
// DefaultLit reads. This pass's own inputs (depth, normal, output UAV, sampler, params) are set 0.
//
// NOTE (#124 Inc 2): the hit-resolve/shade helpers below are a TEMPORARY copy of DefaultLit's
// ResolveHit / ShadeSurfaceHit / RayTraceShadow. Inc 3 deletes the inline GI from DefaultLit and extracts
// these into a shared .hlsli both shaders include — done there (not here) so the extraction happens while
// already editing DefaultLit, rather than refactoring the live forward path mid-increment.

static const float PI = 3.14159265359;

#define GI_RAY_COUNT 2

// ---- Set 0: this pass's own resources ----
// One G-buffer color image carries BOTH the world normal (.xyz) and the NDC depth (.w) — see
// DepthNormal.frag. Sampling one plain color image (not the depth-stencil attachment) sidesteps the
// DEPTH_STENCIL_READ_ONLY-vs-SHADER_READ_ONLY layout mismatch a compute sampled-image descriptor rejects.
Texture2D<float4> GBufferNormal : register(t0, space0); // .xyz = world normal, .w = NDC depth
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u1, space0); // half-res irradiance (rgb)
SamplerState LinearSampler : register(s2, space0);       // bindless albedo / cubemap sampling

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
	uint2 _Pad;
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder) ----
Texture2D Textures[] : register(t0, space3);
TextureCube Cubemaps[] : register(t1, space3);
RaytracingAccelerationStructure SceneTLAS : register(t2, space3);

uint64_t GeoTableAddress()
{
	return (uint64_t(ReflGeoTableAddrHi) << 32) | uint64_t(ReflGeoTableAddrLo);
}

// --- Geometry table read (mirrors GeometryRecord, 112-byte stride) — TEMPORARY copy, see header note. ---
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

float2 LoadVertexUV(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 24ull;
	return float2(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4));
}

float3 LoadVertexNormal(uint64_t vertexAddr, uint index)
{
	const uint64_t a = vertexAddr + uint64_t(index) * 48ull + 12ull;
	return float3(vk::RawBufferLoad<float>(a, 4), vk::RawBufferLoad<float>(a + 4, 4), vk::RawBufferLoad<float>(a + 8, 4));
}

struct HitSurface
{
	float3 Albedo;
	float3 Nw;
};

HitSurface ResolveHit(uint64_t tableAddr, uint instanceId, uint prim, float2 bary)
{
	const GeoRecord rec = LoadGeoRecord(tableAddr, instanceId);

	const uint64_t idxBase = rec.IndexAddress + uint64_t(prim) * 12ull;
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
	const float3 nObj = w * LoadVertexNormal(rec.VertexAddress, i0) + bary.x * LoadVertexNormal(rec.VertexAddress, i1) + bary.y * LoadVertexNormal(rec.VertexAddress, i2);
	s.Nw = normalize(mul(nObj, (float3x3)rec.Model));
	return s;
}

// Shadow ray for the one-bounce hit shading (ACCEPT_FIRST_HIT: occlusion only).
float RayTraceShadowGI(float3 positionWS, float3 Ng, float3 L, float tMax)
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

// Shade a committed GI-ray hit as lit surface radiance (albedo * (sun-with-shadow + IBL ambient)). One
// bounce; the hit's ambient is the cheap IBL/flat term (no recursion).
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
			const float sh = RayTraceShadowGI(hitPos, s.Nw, Lsun, 1e30);
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
		ambient = float3(0.03, 0.03, 0.03);
	}

	return s.Albedo * (direct + ambient);
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

	const float4 gbuf = GBufferNormal.SampleLevel(LinearSampler, uv, 0);
	const float depth = gbuf.w; // NDC depth packed into .w by the prepass
	// Sky / no geometry: the prepass cleared the color target to 0, so .w (depth) is 0 there — but a real
	// far-plane fragment has depth ~1. Distinguish "nothing drawn" (zero normal) from geometry: a zero-length
	// normal means the pixel is sky. Also treat depth >= 1 (far plane) as sky.
	if (dot(gbuf.xyz, gbuf.xyz) < 1e-6 || depth >= 1.0)
	{
		GIOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as Sky.frag: NDC xy in [-1,1],
	// z = raw depth, w = 1, then perspective divide). NDC.y is NOT flipped — matches the sky reconstruction.
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	const float3 N = normalize(gbuf.xyz);
	const float3 Ng = N; // the prepass stores the geometric-ish vertex normal; reuse for the ray offset

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

	float3 incoming = float3(0, 0, 0);
	[unroll] for (int s = 0; s < GI_RAY_COUNT; ++s)
	{
		const float u1 = frac((float(s) + ign) / float(GI_RAY_COUNT));
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

		RayQuery<RAY_FLAG_CULL_NON_OPAQUE> q;
		q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
		q.Proceed();

		if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT && tableAddr != 0)
		{
			const float3 hitPos = origin + dir * q.CommittedRayT();
			incoming += ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos);
		}
		else if (PrefilteredCubeIndex != 0)
		{
			incoming += Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb * IBLIntensity;
		}
	}

	// Incoming irradiance, intensity-scaled. NO receiver albedo — that's multiplied at full res in the
	// forward pass after the bilateral upsample, so half-res GI never blurs albedo edges. GIIntensity is a
	// linear scalar (no effect on edges), applied here so the debug view shows the tuned signal.
	const float3 irradiance = (incoming / float(GI_RAY_COUNT)) * GIIntensity;
	GIOut[id.xy] = float4(irradiance, 1.0);
}
