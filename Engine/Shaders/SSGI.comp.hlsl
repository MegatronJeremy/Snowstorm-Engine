// Screen-space global illumination, compute stage (#151). The raster BASELINE the thesis compares RT GI
// (GI.comp.hlsl) against: it reads the SAME depth+normal G-buffer and writes the SAME half-res GITarget
// (incoming irradiance .rgb, NO receiver albedo), so the shared GI tail (temporal accumulation, a-trous
// denoise, bilateral upsample) and the forward consumption are identical for both producers. The only
// variable in the A/B is where the incoming radiance came from: a screen-space march vs a ray trace.
//
// Per half-res pixel: reconstruct the receiver world position from depth + InvViewProj, build a tangent
// basis around the geometric normal, draw RayCount cosine-weighted hemisphere directions, and MARCH the
// depth buffer along each (SSR's coarse+binary-refine crossing test). A hit samples the PREVIOUS frame's
// scene color at the hit pixel, reprojected by velocity: the classic one-bounce prev-color gather
// (CryEngine / UE4 r.SSGI.Enable). Off-screen, behind-camera, see-through, and backface hits fall back to
// the prefiltered env cube along the ray, the same miss GI.comp takes, so the two producers agree on the
// ambient floor and the A/B isolates the bounce term.
//
// No RayQuery / TLAS, so it compiles in the base permutation and runs on non-RT devices. Set 0 = this pass's
// inputs; set 3 = the bindless Cubemaps[] for the miss (gap-filled by the compute pipeline builder).
//
// Inherent limits of a screen-space bounce, expected and not bugs: geometry outside the frustum or behind
// a nearer surface contributes nothing (the fallback is the cube), and the radiance is one frame stale and
// view-dependent (screen color is what that surface emitted toward the CAMERA, reused as if omnidirectional).

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky + LinearizeViewDepth

static const float PI = 3.14159265359;

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GBufferNormal : register(t0, space0); // .xy = oct GEOMETRIC normal (matches GI.comp's receiver basis)
Texture2D<float> GBufferDepth : register(t1, space0);   // fp32 NDC depth (D32 attachment)
Texture2D<float4> PrevSceneColor : register(t2, space0); // previous frame's resolved HDR scene color (linear, pre-tonemap)
Texture2D<float4> Velocity : register(t3, space0);       // .xy = screen-space motion (curr_uv - prev_uv)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u4, space0); // half-res irradiance (rgb)
SamplerState LinearSampler : register(s5, space0);       // prev-color + cubemap sampling (clamp-linear)

cbuffer SSGICB : register(b6, space0)
{
	float4x4 InvViewProj; // clip -> world (depth -> world position)
	float4x4 ViewProj;    // world -> clip (project a march sample to screen)
	float3 CameraPosition; // unused by the gather; kept for parity/debug with GICB
	float GIRange;         // gather ray max distance (world units), render.gi.range

	uint2 OutSize;        // half-res dispatch dimensions
	float Near;           // camera near/far to linearize NDC depth for the crossing/thickness test
	float Far;

	float GIIntensity;    // scales the indirect contribution, render.gi.intensity
	uint FrameCounter;    // per-frame sample rotation (the GI temporal pass converges the few rays)
	uint RayCount;        // hemisphere-gather rays per pixel this frame (render.gi.rays, clamped [1,16])
	uint MaxSteps;        // coarse march steps along each ray

	uint RefineSteps;          // binary-refinement iterations after a crossing
	float Thickness;           // depth-crossing acceptance window (world units): reject seeing-through thin geometry
	uint PrefilteredCubeIndex; // bindless prefiltered env cube for the miss (0 = black)
	float IBLIntensity;        // scales the miss cube, matching GI.comp's sky-miss bounce
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder). No SceneTLAS -> non-RT. ----
Texture2D Textures[] : register(t0, space3);   // declared for set-3 layout parity (unused here)
TextureCube Cubemaps[] : register(t1, space3); // prefiltered env cube for the gather miss

float3 ReconstructWorld(float2 uv, float ndcDepth)
{
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, ndcDepth, 1.0), InvViewProj);
	return worldH.xyz / worldH.w;
}

// Screen-edge fade: a gather ray that lands near a border saw only a sliver of the off-screen world, so blend
// its radiance back toward the cube instead of hard-cutting. 1 in the interior, ramps over the outer 10%.
float ScreenEdgeFade(float2 uv)
{
	const float2 d = min(uv, 1.0 - uv);
	const float2 f = saturate(d / 0.1);
	return f.x * f.y;
}

float3 SampleEnvCube(float3 dir)
{
	if (PrefilteredCubeIndex == 0)
	{
		return float3(0, 0, 0);
	}
	return Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb * IBLIntensity;
}

// One screen-space gather ray. Returns the incoming radiance along `dir`: the reprojected previous-frame color
// at the first accepted depth crossing, or the env cube on any miss. `fullDims` is the full-res G-buffer extent
// (the march samples it directly; the output grid is half-res).
float3 GatherRay(float3 origin, float3 dir, uint2 fullDims)
{
	const uint maxSteps = max(MaxSteps, 1u);
	const float stepWorld = GIRange / float(maxSteps);

	float tPrev = 0.0;
	float prevDeltaRel = -1.0; // (ray_view_z - scene_view_z - bias) at the previous step; negative = ray in front
	bool hit = false;
	float2 hitUV = float2(0, 0);

	[loop] for (uint i = 1; i <= maxSteps; ++i)
	{
		const float t = float(i) * stepWorld;
		const float3 sampleWS = origin + dir * t;

		float4 clip = mul(float4(sampleWS, 1.0), ViewProj);
		if (clip.w <= 0.0)
		{
			break; // behind the camera
		}
		const float2 sampUV = (clip.xy / clip.w) * 0.5 + 0.5;
		if (any(sampUV < 0.0) || any(sampUV > 1.0))
		{
			break; // ray left the screen -> miss (env cube)
		}

		const int2 sampTexel = clamp(int2(sampUV * float2(fullDims)), int2(0, 0), int2(fullDims) - 1);
		const float sceneNDC = GBufferDepth.Load(int3(sampTexel, 0)).r;
		if (IsSky(sceneNDC))
		{
			tPrev = t;
			prevDeltaRel = -1.0; // sky is infinitely far: the ray is "in front"
			continue;
		}

		const float rayLin = LinearizeViewDepth(clip.z / clip.w, Near, Far);
		const float sceneLin = LinearizeViewDepth(sceneNDC, Near, Far);
		// Self-hit bias, RELATIVE to view depth. A cosine-hemisphere ray can leave near-tangent to the surface
		// and hug it for many steps, where rayLin and sceneLin differ only by float noise. An absolute epsilon
		// either lets that register as a hit near the camera (the receiver samples ITSELF: a grey self-lit wash)
		// or over-rejects real contact bounce far away, since NDC-derived linear depth error scales with range.
		// 2% of view depth is the same scale-invariant bias SSAO-class effects use.
		const float bias = max(0.02 * sceneLin, 0.02);
		const float deltaRel = (rayLin - sceneLin) - bias; // > 0 => ray is behind the stored surface

		if (deltaRel > 0.0 && prevDeltaRel <= 0.0)
		{
			// Crossed the surface between tPrev and t. Binary-refine the crossing to a precise UV.
			float tLo = tPrev;
			float tHi = t;
			[loop] for (uint r = 0; r < RefineSteps; ++r)
			{
				const float tMid = 0.5 * (tLo + tHi);
				const float3 midWS = origin + dir * tMid;
				float4 midClip = mul(float4(midWS, 1.0), ViewProj);
				const float2 midUV = (midClip.xy / midClip.w) * 0.5 + 0.5;
				const int2 midTexel = clamp(int2(midUV * float2(fullDims)), int2(0, 0), int2(fullDims) - 1);
				const float midSceneLin = LinearizeViewDepth(GBufferDepth.Load(int3(midTexel, 0)).r, Near, Far);
				const float midRayLin = LinearizeViewDepth(midClip.z / midClip.w, Near, Far);
				if (midRayLin - midSceneLin > 0.0)
				{
					tHi = tMid;
				}
				else
				{
					tLo = tMid;
				}
			}

			const float3 hitWS = origin + dir * tHi;
			float4 hitClip = mul(float4(hitWS, 1.0), ViewProj);
			hitUV = (hitClip.xy / hitClip.w) * 0.5 + 0.5;
			const int2 hitTexel = clamp(int2(hitUV * float2(fullDims)), int2(0, 0), int2(fullDims) - 1);
			const float hitSceneLin = LinearizeViewDepth(GBufferDepth.Load(int3(hitTexel, 0)).r, Near, Far);
			const float hitRayLin = LinearizeViewDepth(hitClip.z / hitClip.w, Near, Far);

			// Thickness gate: the ray must land WITHIN the surface's thickness window. A large gap means it
			// passed behind thin geometry into the background, not a real bounce.
			// Backface gate: the stored surface must face the incoming ray. The screen color at a backfacing
			// texel is the radiance its FRONT emitted somewhere else entirely, so accepting it leaks light
			// through walls, the dominant screen-space GI artifact if left unchecked.
			const float3 hitN = DecodeNormalOct(GBufferNormal.Load(int3(hitTexel, 0)).xy);
			if (abs(hitRayLin - hitSceneLin) <= Thickness && dot(hitN, dir) < 0.0 &&
			    all(hitUV >= 0.0) && all(hitUV <= 1.0))
			{
				hit = true;
			}
			break; // stop at the first crossing either way (accepted, or rejected as see-through/backface)
		}

		tPrev = t;
		prevDeltaRel = deltaRel;
	}

	if (!hit)
	{
		return SampleEnvCube(dir);
	}

	// Reproject the hit pixel into the previous frame by that surface's motion (prevUV = uv - velocity, the
	// engine's top-left-UV convention) and read its radiance. The current frame's lit color does not exist
	// yet (this pass runs before Forward), which is why the bounce source is the previous frame at all.
	const float2 vel = Velocity.SampleLevel(LinearSampler, hitUV, 0).xy;
	const float2 prevUV = saturate(hitUV - vel);
	float3 radiance = PrevSceneColor.SampleLevel(LinearSampler, prevUV, 0).rgb;

	// The history is uninitialized on the first frame, and a disoccluded tap can carry garbage.
	if (any(isnan(radiance)) || any(isinf(radiance)))
	{
		return SampleEnvCube(dir);
	}

	// Sub-unity feedback gain. The previous frame's color already contains the previous frame's GI, so this
	// gather feeds on its own output: per-frame gain is albedo * kFeedbackGain * GIIntensity and the steady
	// state is direct / (1 - gain), i.e. a slow brightening that diverges once that product reaches 1. UE4's
	// SSGI has the same loop and exposes GI Intensity as the damping knob; CryEngine's SVOGI calls it the
	// injection multiplier. The knob here is fixed rather than exposed because the RT twin (GI.comp) shades
	// its hits directly and is strictly SINGLE-bounce: an uncontrolled multi-bounce term on this side would
	// be a confound in the A/B, not a feature. Halving keeps the second bounce visible while bounding
	// worst-case amplification at 2x. The structurally correct fix is to feed back a direct-only buffer (the
	// original SSDO formulation), which needs a second render target and a split composite.
	const float kFeedbackGain = 0.5;
	radiance *= kFeedbackGain;

	return lerp(SampleEnvCube(dir), radiance, ScreenEdgeFade(hitUV));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// POINT-fetch the full-res G-buffer at the nearest texel to this half-res pixel's center, never bilinear:
	// a linear tap blends depth across silhouettes and reconstructs a world position in mid-air (#129 Inc 2c).
	uint2 fullDims;
	GBufferDepth.GetDimensions(fullDims.x, fullDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(fullDims)), int2(0, 0), int2(fullDims) - 1);
	const float depth = GBufferDepth.Load(int3(gbTexel, 0)).r;

	// Sky / no geometry. Matches GI.comp's sky write exactly (including .a = 0), because GIDenoise reads .a as
	// SVGF variance and the shared tail must see the same value distribution from either producer.
	if (IsSky(depth))
	{
		GIOut[id.xy] = float4(0, 0, 0, 0);
		return;
	}

	const float3 positionWS = ReconstructWorld(uv, depth);
	const float3 N = DecodeNormalOct(GBufferNormal.Load(int3(gbTexel, 0)).xy);

	// Orthonormal basis (tangent, bitangent, N) to orient the cosine hemisphere. Same construction as GI.comp
	// so both producers draw the same directions for a given pixel and the A/B compares the gather, not the
	// sampling pattern.
	const float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
	const float3 tangent = normalize(cross(up, N));
	const float3 bitangent = cross(N, tangent);

	const float2 px = float2(id.xy) + float2(FrameCounter * 5.588238, FrameCounter * 3.539418);
	const float ign = frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));

	const float3 origin = positionWS + N * 0.05;

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

		incoming += GatherRay(origin, dir, fullDims);
	}

	// Incoming irradiance, intensity-scaled. NO receiver albedo: the forward pass multiplies it at full res
	// after the bilateral upsample, so half-res GI never blurs albedo edges. Same contract as GI.comp.
	const float3 irradiance = (incoming / float(rayCount)) * GIIntensity;
	GIOut[id.xy] = float4(irradiance, 1.0);
}
