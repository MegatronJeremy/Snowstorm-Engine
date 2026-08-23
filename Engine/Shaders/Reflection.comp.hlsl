// Full-resolution ray-traced reflection, compute stage (#129). The inline RayTraceReflection from
// DefaultLit.frag.hlsl lifted into a standalone pass over the depth+normal G-buffer, so the traced
// reflection lives in a persistent buffer a temporal denoiser can reproject (the reflection analogue of
// #124's GI separation). Per full-res pixel: reconstruct the receiver's world position from the G-buffer
// depth + InvViewProj, read its world normal, reflect the view vector, trace ONE sharp reflection ray
// against the bindless SceneTLAS, shade the committed hit as lit surface radiance (ShadeSurfaceHit) or
// reflect the prefiltered sky on a miss, and write RAW reflected radiance (.rgb) + the hit distance (.a).
//
// RAW radiance only: NOT multiplied by the Fresnel/BRDF split-sum weight, ReflIntensity, or the
// roughness falloff. Those stay in the forward pass (DefaultLit ComputeIBL), applied per-pixel at full
// res, exactly as #124 keeps albedo out of the half-res GI buffer. The forward pass samples this target by
// screen UV and does `lerp(envCubeSpecular, sampled * specWeight * ReflIntensity, reflWeight)`.
//
// SHARP ray (no glossy cone jitter): a mirror ray needs only normal+depth (both in the G-buffer), so the
// trace stays roughness-free + deterministic. The forward's reflWeight already fades rough surfaces onto
// the blurry prefiltered env cube, so smooth surfaces get a sharp RT reflection and rough ones hand off to
// the cube (a roughness-driven glossy blur is a deferred follow-up: #129 Inc 3 / a separate issue).
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
Texture2D<float4> GBufferNormal : register(t0, space0);  // main: .xy oct GEOMETRIC normal, .z roughness, .w UNUSED
Texture2D<float4> GBufferShading : register(t1, space0); // .xy oct NORMAL-MAPPED shading normal
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ReflOut : register(u2, space0); // .rgb radiance, .a hitT
SamplerState LinearSampler : register(s3, space0);       // bindless albedo / cubemap sampling
Texture2D<float> GBufferDepth : register(t5, space0);    // fp32 NDC depth (D32 attachment), sampled directly

cbuffer ReflCB : register(b4, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth->world-position reconstruction
	float3 CameraPosition; // reflection needs the view vector V = normalize(camPos - posWS)
	float ReflRange;       // reflection ray max distance (world units)

	uint2 OutSize;         // full-res dispatch dimensions
	float ReflConeScale;   // how much roughness widens the glossy jitter cone (render.reflections.cone_scale)
	uint FrameCounter;     // per-frame rotation of the glossy jitter (temporal accumulation averages it)

	// Sun (DirectionalLights[0]) for the one-bounce hit shading - consumed by RTHitShading.hlsli.
	float3 SunDirection;
	float SunIntensity;
	float3 SunColor;
	float ShadowStrength;

	// IBL + geometry table.
	uint IrradianceCubeIndex;  // bindless cube for hit ambient (0 = flat fill) - used by RTHitShading.hlsli
	uint PrefilteredCubeIndex; // bindless cube for the sky-miss reflection (0 = black)
	float IBLIntensity;
	uint LightCount;

	uint ReflGeoTableAddrLo; // device address of the GeometryRecord table (lo/hi)
	uint ReflGeoTableAddrHi;
	uint RayCount;           // reflection rays per pixel this frame (render.reflections.rays, clamped [1,16])
	uint _Pad1;
};

// Set 3 bindless + geometry-table read + one-bounce hit shading, shared with the GI compute pass (#129).
// The CB above provides every scalar RTHitShading.hlsli's contract requires; LinearSampler is on set 0.
#include "Include/RTHitShading.hlsli"
#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

static const float PI = 3.14159265359;

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

	// Reflection is FULL-res, 1:1 with the G-buffer, so POINT-fetch (Load) both G-buffers by integer texel
	// and never bilinear-sample depth/normal. A linear tap blends across silhouettes (midpoint depth -> a
	// reconstructed world position in mid-air -> a garbage reflection that bleeds a pixel past the edge). This
	// was the "edge bleeding" on reflections + GI (#129 Inc 2c). The bindless albedo/cubemap fetches in the hit
	// shading still use LinearSampler; only the G-buffer reconstruction must be point-sampled.
	const float4 mainGB = GBufferNormal.Load(int3(id.xy, 0));   // .z roughness, .w unused
	const float depth = GBufferDepth.Load(int3(id.xy, 0)).r;   // fp32 depth from the D32 attachment (was mainGB.w)
	const float roughness = mainGB.z;                          // perceptual roughness (#129 Inc 1c)

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

	// #129 Inc 1c: reflect off the NORMAL-MAPPED shading normal (separate target). This fixes "reflections
	// look flat / shift with angle". AO/GI use the geometric normal in the main G-buffer; reflections need the
	// bumped one to match DefaultLit's shading.
	const float3 N = DecodeNormalOct(GBufferShading.Load(int3(id.xy, 0)).xy); // point-fetch (see above)
	const float3 V = normalize(CameraPosition - positionWS);
	const float3 R = reflect(-V, N); // mirror reflection vector

	const uint64_t tableAddr = GeoTableAddress();

	// Glossy cone jitter (#129 Inc 2b): a SHARP one-ray-per-pixel mirror trace ALIASES the reflected scene into
	// a pixelated grid (the "blocky" raw buffer). Perturb each ray within a roughness-scaled disk around R,
	// with a per-frame + per-sample IGN rotation, and AVERAGE render.reflections.rays samples this frame: more
	// rays converge the glossy cone in-frame (less shimmer under motion, less temporal reliance) at ~linear
	// cost. roughness == 0 (a perfect mirror) => zero cone => every sample is the exact sharp ray, so the loop
	// collapses to one deterministic result (averaging identical samples is a no-op) and true mirrors stay crisp.
	const uint rayCount = max(RayCount, 1u); // >= 1 by the C++ clamp; guards the /rayCount below
	const float3 up = abs(R.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, R));
	const float3 bitangent = cross(R, tangent);
	const float coneRadius = roughness * ReflConeScale;

	float3 radianceSum = float3(0, 0, 0);
	float hitTSum = 0.0;
	[loop] for (uint s = 0; s < rayCount; ++s)
	{
		float3 dir = R;
		if (roughness > 0.0)
		{
			// Decorrelate per sample AND per frame so the temporal pass keeps averaging fresh directions.
			const float2 px = float2(id.xy) + float2((FrameCounter * rayCount + s) * 5.588238, (FrameCounter * rayCount + s) * 3.539418);
			const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
			const float rr = coneRadius * sqrt(ign);
			const float phi = 2.0 * PI * frac(ign + 0.61803398875); // golden-ratio decorrelation of angle vs radius
			dir = normalize(R + (rr * cos(phi)) * tangent + (rr * sin(phi)) * bitangent);
		}

		RayDesc ray;
		ray.Origin = positionWS + N * 0.02 + dir * 0.01; // normal-offset to dodge self-hit
		ray.Direction = dir;
		ray.TMin = 0.0;
		ray.TMax = ReflRange;

		// Closest hit (no ACCEPT_FIRST_HIT): a reflection needs the FRONT-MOST surface along the ray. Alpha-test
		// cutout occluders (masked instances are FORCE_NON_OPAQUE, surfacing as candidates); opaque hits
		// auto-commit so the loop body only runs for masked geometry.
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
			const float hitT = q.CommittedRayT();
			const float3 hitPos = ray.Origin + dir * hitT;
			radianceSum += ShadeSurfaceHit(tableAddr, q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), hitPos, 1.0);
			hitTSum += hitT;
		}
		else
		{
			// Miss (or no geometry table): reflect the distant sky along the (jittered) direction. Large hit
			// distance = "miss" for temporal.
			float3 sky = float3(0, 0, 0);
			if (PrefilteredCubeIndex != 0)
			{
				sky = Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb;
			}
			radianceSum += sky;
			hitTSum += ReflRange;
		}
	}

	ReflOut[id.xy] = float4(radianceSum / float(rayCount), hitTSum / float(rayCount));
}
