// Screen-space reflections, compute stage (#151). The raster BASELINE the thesis compares RT reflections
// (Reflection.comp.hlsl) against: it reads the SAME depth+shading-normal G-buffer and writes the SAME
// full-res ReflectionTarget (float4 = raw reflected radiance .rgb + hit distance .a), so the forward pass
// (DefaultLit ComputeIBL) blends it identically to the RT path — DefaultLit is agnostic to which produced it.
//
// Per full-res pixel: reconstruct the receiver world position from depth + InvViewProj, reflect the view
// vector off the NORMAL-MAPPED shading normal, then MARCH the depth buffer along that reflection ray. A hit
// (the ray crosses in front of the stored surface within a thickness window) reflects the PREVIOUS frame's
// scene color at the hit pixel, reprojected by the velocity buffer (SSR is consumed before the current
// forward runs, so the current lit color doesn't exist yet — the standard forward-renderer SSR source). A
// miss / off-screen ray / behind-thickness reflects the prefiltered env cube, exactly like Reflection.comp's
// miss, so smooth surfaces hand off to the cube at the screen edges instead of going black. Screen-edge fade
// blends hits toward the cube near the borders (reflections can't see off-screen geometry).
//
// No RayQuery / TLAS — this is the non-RT path (compiles in the base permutation). The march is in WORLD
// space with a binary refinement (needs only InvViewProj/ViewProj + near/far; no view/proj split), a
// readable baseline; a perspective-correct screen-space DDA is the higher-quality follow-up. Set 0 = this
// pass's inputs; set 3 = the bindless Cubemaps[] for the env miss (gap-filled by the compute pipeline
// builder; no TLAS binding, so it stays non-RT).

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky + LinearizeViewDepth

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GBufferShading : register(t0, space0); // .xy = oct NORMAL-MAPPED shading normal
Texture2D<float> GBufferDepth : register(t1, space0);    // fp32 NDC depth (D32 attachment)
Texture2D<float4> PrevSceneColor : register(t2, space0); // previous frame's resolved HDR scene color
Texture2D<float4> Velocity : register(t3, space0);       // .xy = screen-space motion (curr_uv - prev_uv)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> ReflOut : register(u4, space0); // .rgb radiance, .a hitT
SamplerState LinearSampler : register(s5, space0);       // prev-color + cubemap sampling (clamp-linear)

cbuffer SSRCB : register(b6, space0)
{
	float4x4 InvViewProj; // clip -> world (depth -> world position)
	float4x4 ViewProj;    // world -> clip (project a march sample to screen)
	float3 CameraPosition; // for the view vector V = normalize(camPos - posWS)
	float ReflRange;       // reflection ray max distance (world units) — render.reflections.range

	uint2 OutSize;         // full-res dispatch dimensions
	float Near;            // camera near/far to linearize NDC depth for the crossing/thickness test
	float Far;

	float Thickness;          // depth-crossing acceptance window (world units): reject seeing-through thin geometry
	uint MaxSteps;            // coarse march steps along the ray
	uint RefineSteps;         // binary-refinement iterations after a crossing
	uint PrefilteredCubeIndex; // bindless prefiltered env cube for the miss (0 = black)
};

// ---- Set 3: engine bindless pool (gap-filled by the compute pipeline builder). No SceneTLAS -> non-RT. ----
Texture2D Textures[] : register(t0, space3);      // declared for set-3 layout parity (unused here)
TextureCube Cubemaps[] : register(t1, space3);    // prefiltered env cube for the reflection miss

float3 ReconstructWorld(float2 uv, float ndcDepth)
{
	const float2 ndc = uv * 2.0 - 1.0;
	float4 worldH = mul(float4(ndc, ndcDepth, 1.0), InvViewProj);
	return worldH.xyz / worldH.w;
}

// Screen-edge fade: reflections vanish where the ray hits near a screen border (no off-screen data). 1 in the
// interior, ramps to 0 at the edges over ~10% of the screen.
float ScreenEdgeFade(float2 uv)
{
	const float2 d = min(uv, 1.0 - uv);        // distance to the nearest border in each axis
	const float2 f = saturate(d / 0.1);        // ramp over the outer 10%
	return f.x * f.y;
}

float3 SampleEnvCube(float3 dir)
{
	if (PrefilteredCubeIndex == 0)
	{
		return float3(0, 0, 0);
	}
	return Cubemaps[NonUniformResourceIndex(PrefilteredCubeIndex)].SampleLevel(LinearSampler, dir, 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// Point-fetch the receiver G-buffer (1:1 full-res, never bilinear across silhouettes).
	const float depth = GBufferDepth.Load(int3(id.xy, 0)).r;

	// Sky / no geometry -> no reflection (matches Reflection.comp: 0 radiance + a "miss" hit distance).
	if (IsSky(depth))
	{
		ReflOut[id.xy] = float4(0, 0, 0, ReflRange);
		return;
	}

	const float3 positionWS = ReconstructWorld(uv, depth);
	const float3 N = DecodeNormalOct(GBufferShading.Load(int3(id.xy, 0)).xy); // normal-mapped shading normal
	const float3 V = normalize(CameraPosition - positionWS);
	const float3 R = reflect(-V, N); // mirror reflection direction (world space)

	// March origin offset off the surface along the normal to dodge immediate self-intersection.
	const float3 origin = positionWS + N * 0.05;

	const uint maxSteps = max(MaxSteps, 1u);
	const float stepWorld = ReflRange / float(maxSteps);

	// Coarse linear march: step along R, project each sample to screen, compare the ray's linearized view depth
	// against the stored surface's. A crossing (ray goes from in-front to behind the surface) between two steps
	// brackets a potential hit. World-space uniform steps -> uneven screen coverage (the readable-baseline
	// tradeoff; a screen-space DDA is the follow-up), so use enough steps + the binary refine below.
	float tPrev = 0.0;
	float prevDelta = -1.0; // ray_view_z - scene_view_z at the previous step (negative = ray in front)
	bool hit = false;
	float2 hitUV = float2(0, 0);
	float hitT = ReflRange;

	[loop] for (uint i = 1; i <= maxSteps; ++i)
	{
		const float t = float(i) * stepWorld;
		const float3 sampleWS = origin + R * t;

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

		const int2 sampTexel = clamp(int2(sampUV * float2(OutSize)), int2(0, 0), int2(OutSize) - 1);
		const float sceneNDC = GBufferDepth.Load(int3(sampTexel, 0)).r;
		if (IsSky(sceneNDC))
		{
			tPrev = t;
			prevDelta = -1.0; // sky is infinitely far: ray is "in front"
			continue;
		}

		const float rayLin = LinearizeViewDepth(clip.z / clip.w, Near, Far);
		const float sceneLin = LinearizeViewDepth(sceneNDC, Near, Far);
		const float delta = rayLin - sceneLin; // > 0 => ray is behind the stored surface

		if (delta > 0.0 && prevDelta <= 0.0)
		{
			// Crossed the surface between tPrev and t. Binary-refine the crossing to a precise UV/distance.
			float tLo = tPrev;
			float tHi = t;
			[loop] for (uint r = 0; r < RefineSteps; ++r)
			{
				const float tMid = 0.5 * (tLo + tHi);
				const float3 midWS = origin + R * tMid;
				float4 midClip = mul(float4(midWS, 1.0), ViewProj);
				const float2 midUV = (midClip.xy / midClip.w) * 0.5 + 0.5;
				const int2 midTexel = clamp(int2(midUV * float2(OutSize)), int2(0, 0), int2(OutSize) - 1);
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

			const float3 hitWS = origin + R * tHi;
			float4 hitClip = mul(float4(hitWS, 1.0), ViewProj);
			hitUV = (hitClip.xy / hitClip.w) * 0.5 + 0.5;
			const float hitSceneLin = LinearizeViewDepth(GBufferDepth.Load(int3(clamp(int2(hitUV * float2(OutSize)), int2(0, 0), int2(OutSize) - 1), 0)).r, Near, Far);
			const float hitRayLin = LinearizeViewDepth(hitClip.z / hitClip.w, Near, Far);

			// Thickness gate: accept only if the ray landed WITHIN the surface's thickness window. A large gap
			// means the ray passed behind a thin object into the background -> not a real reflection (see-through).
			if (abs(hitRayLin - hitSceneLin) <= Thickness && all(hitUV >= 0.0) && all(hitUV <= 1.0))
			{
				hit = true;
				hitT = tHi;
			}
			break; // stop at the first crossing either way (accepted, or rejected as see-through)
		}

		tPrev = t;
		prevDelta = delta;
	}

	if (hit)
	{
		// Reflect the PREVIOUS frame's color at the hit pixel, reprojected by that surface's motion
		// (prevUV = uv - velocity, the engine's top-left-UV convention). Off-screen last frame -> the reprojected
		// UV clamps and the edge fade + reflection temporal denoiser hide the disocclusion.
		const float2 vel = Velocity.SampleLevel(LinearSampler, hitUV, 0).xy;
		const float2 prevUV = saturate(hitUV - vel);
		float3 radiance = PrevSceneColor.SampleLevel(LinearSampler, prevUV, 0).rgb;

		// Sanitize: the history is uninitialized on the very first frame (and disoccluded taps can be garbage).
		if (any(isnan(radiance)) || any(isinf(radiance)))
		{
			radiance = SampleEnvCube(R);
		}

		// Fade the hit toward the env cube near the screen borders (no off-screen data there).
		const float edge = ScreenEdgeFade(hitUV);
		const float3 outColor = lerp(SampleEnvCube(R), radiance, edge);
		ReflOut[id.xy] = float4(outColor, hitT);
	}
	else
	{
		// Miss / off-screen / see-through -> the prefiltered env cube along R, same as Reflection.comp's miss.
		ReflOut[id.xy] = float4(SampleEnvCube(R), ReflRange);
	}
}
