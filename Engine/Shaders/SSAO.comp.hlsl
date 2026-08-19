// Screen-space ambient occlusion, compute stage (#151). The raster BASELINE the thesis compares RT AO
// (AO.comp.hlsl) against: it reads the SAME depth+normal G-buffer and writes the SAME half-res AOTarget, so
// the forward pass is agnostic to which technique produced the occlusion. No RayQuery / TLAS / geometry table
// — this is the non-RT path. Per output pixel (at render.ao.scale of the viewport): reconstruct world
// position from depth + InvViewProj (same convention as AO.comp / GI.comp), read the world normal, sample a
// normal-oriented hemisphere kernel of the DEPTH BUFFER (project each kernel point back to screen, compare its
// view-depth against the stored surface), range-check, accumulate occlusion, and write a factor in [0,1]
// (1 = fully open). Sky pixels output 1.
//
// The kernel uses a FRAME-STATIC per-pixel rotation (pixel-coord hash only, no FrameCounter): the structured
// noise it leaves is removed by the depth+normal bilateral blur (SSAOBlur.comp), NOT by temporal accumulation
// — so SSAO is temporally stable on its own and needs no velocity pass / SVGF (the deliberate difference from
// the RT AO path, #151). Classic Crytek/Chapman hemisphere SSAO; HBAO-quality without a separate depth march.

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky + LinearizeViewDepth

static const float PI = 3.14159265359;
static const uint KERNEL_SIZE = 16u; // fixed hemisphere sample count — a representative baseline, no CVar

// ---- Set 0: this pass's own resources (no set 3 — SSAO is not ray traced) ----
Texture2D<float4> GBufferNormal : register(t0, space0); // .xy = oct world normal, .z = roughness, .w = unused
Texture2D<float> GBufferDepth : register(t4, space0);   // fp32 NDC depth (D32 attachment), sampled directly
[[vk::image_format("rgba16f")]] RWTexture2D<float4> AOOut : register(u1, space0); // half-res occlusion factor [0,1] in .r

cbuffer SSAOCB : register(b3, space0)
{
	float4x4 InvViewProj; // clip -> world, for depth -> world-position reconstruction
	float4x4 ViewProj;    // world -> clip, to project a kernel sample back to screen (the depth-buffer lookup)
	uint2 OutSize;        // half-res dispatch dimensions
	float AORadius;       // hemisphere radius (world units) — render.ao.radius
	float AOIntensity;    // scales the darkening (1 = neutral, >1 = artistic boost) — render.ao.intensity
	float Near;           // camera near/far, to linearize NDC depth for the range/occlusion test
	float Far;
	float Bias;           // view-depth self-occlusion bias (world units) to avoid acne on flat surfaces
	float _Pad0;
	float _Pad1;
	float _Pad2;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// POINT-fetch the full-res G-buffer at the nearest texel (never bilinear — a linear tap blends depth across
	// silhouettes into a mid-air world position). Same rule as AO.comp.
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float4 gbuf = GBufferNormal.Load(int3(gbTexel, 0));
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r;

	// Sky / no geometry -> fully open (AO = 1). Depth-based, matching every other G-buffer consumer.
	if (IsSky(depth))
	{
		AOOut[id.xy] = 1.0;
		return;
	}

	// Reconstruct world position from depth + InvViewProj (same convention as AO.comp / GI.comp).
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
	const float3 positionWS = worldH.xyz / worldH.w;

	const float3 N = DecodeNormalOct(gbuf.xy);
	const float centerLinZ = LinearizeViewDepth(depth, Near, Far); // view-space depth of this pixel's surface

	// Orthonormal basis (tangent, bitangent, N) to orient the cosine hemisphere.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	// Frame-STATIC per-pixel rotation angle (pixel-coord IGN, no FrameCounter): decorrelates neighbouring
	// pixels' kernels so the blur can average them, without the temporal flicker a per-frame seed would add.
	const float ign = frac(52.9829189 * frac(dot(float2(id.xy), float2(0.06711056, 0.00583715))));
	const float rot = ign * 2.0 * PI;
	const float ca = cos(rot);
	const float sa = sin(rot);

	float occlusion = 0.0;
	[unroll] for (uint i = 0; i < KERNEL_SIZE; ++i)
	{
		// Cosine-weighted hemisphere point (z = up). Stratified in u1 across the kernel; u2 spun by the golden
		// ratio + the per-pixel seed so the samples don't line up between pixels.
		const float u1 = (float(i) + 0.5) / float(KERNEL_SIZE);
		const float u2 = frac(float(i) * 0.61803398875 + ign);
		const float r = sqrt(u1);
		const float phi = 2.0 * PI * u2;
		float3 local = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));

		// Cluster samples toward the origin (accelerating interpolation) so near-contact occlusion dominates —
		// the standard SSAO kernel-magnitude curve. i/K in [0,1); squared biases the distribution inward.
		const float t = float(i) / float(KERNEL_SIZE);
		local *= lerp(0.1, 1.0, t * t);

		// Spin the tangent-plane component by the per-pixel angle (frame-static), then lift into world space.
		const float2 spun = float2(local.x * ca - local.y * sa, local.x * sa + local.y * ca);
		const float3 offsetWS = (spun.x * tangent + spun.y * bitangent + local.z * N) * AORadius;
		const float3 samplePosWS = positionWS + offsetWS;

		// Project the sample back to screen and read the actual surface depth there.
		float4 clip = mul(float4(samplePosWS, 1.0), ViewProj);
		if (clip.w <= 0.0)
		{
			continue; // behind the camera
		}
		const float2 sndc = clip.xy / clip.w;
		const float2 suv = sndc * 0.5 + 0.5;
		if (any(suv < 0.0) || any(suv > 1.0))
		{
			continue; // off-screen sample: no information, treat as not occluding
		}

		const int2 sTexel = clamp(int2(suv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
		const float sDepth = GBufferDepth.Load(int3(sTexel, 0)).r;
		if (IsSky(sDepth))
		{
			continue; // sample points at the sky: nothing occludes
		}

		const float sampleLinZ = LinearizeViewDepth(clip.z / clip.w, Near, Far); // view depth of the kernel POINT
		const float storedLinZ = LinearizeViewDepth(sDepth, Near, Far);          // view depth of the real surface

		// Occluded when the real surface is NEARER than the kernel point (the point is buried in geometry),
		// past a small bias to avoid self-occlusion acne. Range check fades occluders far from this pixel in
		// depth so a distant background surface behind a gap doesn't count as an occluder (the classic SSAO
		// halo fix). Both use LINEAR view depth so the test is uniform near and far.
		const float occluded = (storedLinZ <= sampleLinZ - Bias) ? 1.0 : 0.0;
		const float rangeCheck = smoothstep(0.0, 1.0, AORadius / max(abs(centerLinZ - storedLinZ), 1e-4));
		occlusion += occluded * rangeCheck;
	}

	occlusion = (occlusion / float(KERNEL_SIZE)) * AOIntensity;
	const float ao = saturate(1.0 - occlusion);
	// .a = 1 (fully-open hit-distance convention, unused by the bilateral blur; kept so the RGBA16F AOTarget
	// layout matches the RT path's float4(ao,ao,ao,hitT)).
	AOOut[id.xy] = float4(ao, ao, ao, 1.0);
}
