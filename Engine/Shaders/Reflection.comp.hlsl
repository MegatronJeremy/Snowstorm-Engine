// Full-resolution ray-traced reflection, compute stage (#129). The inline RayTraceReflection from
// DefaultLit.frag.hlsl lifted into a standalone pass over the depth+normal G-buffer, so the traced
// reflection lives in a persistent buffer a temporal denoiser can reproject (the reflection analogue of
// #124's GI separation). Per full-res pixel: reconstruct the receiver's world position from the G-buffer
// depth + InvViewProj, read its world normal, reflect the view vector, trace ONE sharp reflection ray
// against the bindless SceneTLAS, shade the committed hit as lit surface radiance (ShadeSurfaceHit) or
// reflect the prefiltered sky on a miss, and write RAW reflected radiance (.rgb) + the hit distance (.a).
//
// RAW radiance only — NOT multiplied by the Fresnel/BRDF split-sum weight, ReflIntensity, or the
// roughness falloff. Those stay in the forward pass (DefaultLit ComputeIBL), applied per-pixel at full
// res, exactly as #124 keeps albedo out of the half-res GI buffer. The forward pass samples this target by
// screen UV and does `lerp(envCubeSpecular, sampled * specWeight * ReflIntensity, reflWeight)`.
//
// SHARP ray (no glossy cone jitter): a mirror ray needs only normal+depth (both in the G-buffer), so the
// trace stays roughness-free + deterministic. The forward's reflWeight already fades rough surfaces onto
// the blurry prefiltered env cube, so smooth surfaces get a sharp RT reflection and rough ones hand off to
// the cube — a roughness-driven glossy blur is a deferred follow-up (#129 Inc 3 / a separate issue).
//
// .a = hit distance (world units), for the temporal pass's depth-aware reject now and a future NRD-style
// reflected-virtual-position reprojection. A miss writes a large sentinel distance.
//
// Compiled only in the SS_RAYTRACING permutation (RayQuery). Set 0 = this pass's inputs; set 3 (bindless
// textures/cubemaps/TLAS) is shared via RTHitShading.hlsli, gap-filled by the compute pipeline builder.

// ---- Set 0: this pass's own resources ----
// #129 Inc 1c: reflections reflect off the NORMAL-MAPPED (shading) normal, in a SEPARATE target from the
// main G-buffer (whose .xy is the GEOMETRIC normal that AO/GI want). This pass reads depth + roughness from
// the main G-buffer and the shading normal from GBufferShading.
Texture2D<float4> GBufferNormal : register(t0, space0);  // main: .xy oct GEOMETRIC normal, .z roughness, .w depth
Texture2D<float4> GBufferShading : register(t1, space0); // .xy oct NORMAL-MAPPED shading normal
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ReflOut : register(u2, space0); // .rgb radiance, .a hitT
SamplerState LinearSampler : register(s3, space0);       // bindless albedo / cubemap sampling

cbuffer ReflCB : register(b4, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	float3 CameraPosition; // reflection needs the view vector V = normalize(camPos - posWS)
	float ReflRange;       // reflection ray max distance (world units)

	uint2 OutSize;         // full-res dispatch dimensions
	float _Pad0;
	uint FrameCounter;     // reserved (sharp ray uses no per-frame rotation; kept for CB parity)

	// Sun (DirectionalLights[0]) for the one-bounce hit shading — consumed by RTHitShading.hlsli.
	float3 SunDirection;
	float SunIntensity;
	float3 SunColor;
	float ShadowStrength;

	// IBL + geometry table.
	uint IrradianceCubeIndex;  // bindless cube for hit ambient (0 = flat fill) — used by RTHitShading.hlsli
	uint PrefilteredCubeIndex; // bindless cube for the sky-miss reflection (0 = black)
	float IBLIntensity;
	uint LightCount;

	uint ReflGeoTableAddrLo; // device address of the GeometryRecord table (lo/hi)
	uint ReflGeoTableAddrHi;
	uint2 _Pad1;
};

// Set 3 bindless + geometry-table read + one-bounce hit shading, shared with the GI compute pass (#129).
// The CB above provides every scalar RTHitShading.hlsli's contract requires; LinearSampler is on set 0.
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

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	const float depth = GBufferNormal.SampleLevel(LinearSampler, uv, 0).w; // NDC depth from the main G-buffer

	// Sky / no geometry (prepass clears depth to 1.0; far plane also ~1.0) -> no reflection. Write 0 radiance
	// + a large hit distance (a "miss" for the temporal depth reject). Depth-based sky test (#129 Inc 1b).
	if (IsSky(depth))
	{
		ReflOut[id.xy] = float4(0, 0, 0, ReflRange);
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as GI.comp / Sky.frag).
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	// #129 Inc 1c: reflect off the NORMAL-MAPPED shading normal (separate target) — the fix for "reflections
	// look flat / shift with angle". AO/GI use the geometric normal in the main G-buffer; reflections need the
	// bumped one to match DefaultLit's shading.
	const float3 N = DecodeNormalOct(GBufferShading.SampleLevel(LinearSampler, uv, 0).xy);
	const float3 V = normalize(CameraPosition - positionWS);
	const float3 R = reflect(-V, N); // sharp mirror reflection vector

	const uint64_t tableAddr = GeoTableAddress();

	RayDesc ray;
	ray.Origin = positionWS + N * 0.02 + R * 0.01; // normal-offset to dodge self-hit
	ray.Direction = R;
	ray.TMin = 0.0;
	ray.TMax = ReflRange;

	// Closest hit (no ACCEPT_FIRST_HIT): a reflection needs the FRONT-MOST surface along the ray.
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE> q;
	q.TraceRayInline(SceneTLAS, RAY_FLAG_NONE, 0xFF, ray);
	q.Proceed();

	if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT && tableAddr != 0)
	{
		const float hitT = q.CommittedRayT();
		const float3 hitPos = ray.Origin + R * hitT;
		const float3 radiance = ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos);
		ReflOut[id.xy] = float4(radiance, hitT);
		return;
	}

	// Miss (or no geometry table): reflect the distant sky along R. Large hit distance = "miss" for temporal.
	float3 sky = float3(0, 0, 0);
	if (PrefilteredCubeIndex != 0)
	{
		sky = Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, R, 0).rgb;
	}
	ReflOut[id.xy] = float4(sky, ReflRange);
}
