// GI temporal accumulation (#125), compute stage — the temporal half of SVGF. Runs at half-res BEFORE the
// à-trous spatial filter: for each half-res pixel, reproject the previous accumulated GI by the motion
// vectors, reject it if a depth-disocclusion test says it's a different surface (reused from the TAA
// resolve, #127), and blend it with this frame's few-ray GITarget trace with a velocity-aware weight. The
// output both feeds the à-trous denoiser AND becomes next frame's history. Over many frames each pixel
// integrates many independent 2-ray noise realizations -> the static/slow-motion shimmer a spatial-only
// filter can't touch converges away.
//
// Why no neighborhood color-clamp (unlike the TAA resolve): the à-trous filter that runs right after IS the
// spatial outlier/variance handler — clamping here too would double-denoise (the issue's explicit warning)
// and fight the à-trous. Ghosting is instead controlled structurally by the depth-disocclusion reject +
// velocity-aware blend, which is what SVGF's temporal stage does. Irradiance-only, like the trace.
//
// Reference: SVGF temporal integration (Schied et al. 2017); the reproject + LinearizeDepth disocclusion is
// lifted from TemporalResolve.frag.hlsl (#44/#127) so the two agree on what a disocclusion is.
//
// This pass's inputs (GI SRV, guide SRV, velocity SRV, history SRV, output UAV, sampler, params) are set 0.
// No TLAS / bindless — a plain image op.

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GICurrent : register(t0, space0);     // this frame's raw half-res GI trace (GITarget)
Texture2D<float4> GBufferNormal : register(t1, space0); // full-res guide: .xyz world normal, .w NDC depth
Texture2D<float4> VelocityTex : register(t2, space0);   // full-res motion: .xy = curr_uv - prev_uv, .z = NDC depth
Texture2D<float4> GIHistoryPrev : register(t3, space0); // previous accumulated GI: .rgb irradiance, .a NDC depth
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u4, space0); // accumulated GI out
SamplerState LinearSampler : register(s5, space0);      // clamp-linear for reprojected history / velocity

cbuffer GITemporalCB : register(b6, space0)
{
	uint2 OutSize;      // half-res dispatch dimensions (== GI extent)
	float HistoryValid; // 1 = blend history, 0 = first frame / reset -> output current only
	float BlendHistory; // base history weight while moving (render.gi.temporal.blend)

	float MaxBlend;     // history weight when ~static (render.gi.temporal.maxblend)
	float Near;         // camera near/far to linearize the packed NDC depths for the disocclusion test
	float Far;
	float DepthRejectScale; // relative view-space threshold (0 = OFF); reuses the TAA #127 value
};

// Linearize an NDC depth (perspectiveRH_ZO, ZO clip [0,1]) to view-space distance — identical to the TAA
// resolve so the two disocclusion tests agree. Guard the denominator so a far/sky texel (d==1) can't /0.
float LinearizeDepth(float d)
{
	return (Near * Far) / max(Far - d * (Far - Near), 1e-6);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const int2 px = int2(id.xy);
	const float2 uv = (float2(px) + 0.5) / float2(OutSize);

	const float3 currentGI = GICurrent.Load(int3(px, 0)).rgb;
	// This pixel's NDC depth from the half-res G-buffer (.w). Packed into the output .a so it becomes next
	// frame's history depth for the reproject below.
	const float depthCurr = GBufferNormal.SampleLevel(LinearSampler, uv, 0).w;

	// First frame / history invalid: nothing to blend, output current — but still write depth into .a so next
	// frame's disocclusion test has a valid previous depth.
	if (HistoryValid < 0.5)
	{
		GIOut[px] = float4(currentGI, depthCurr);
		return;
	}

	// Reproject: prev_uv = uv - velocity. Velocity is a full-res buffer; sample it at this half-res pixel's UV
	// (the motion field is smooth, so the half-res sample is a faithful representative of the footprint).
	const float2 velocity = VelocityTex.SampleLevel(LinearSampler, uv, 0).xy;
	const float2 histUv = uv - velocity;

	// Off-screen history can't be trusted (disocclusion at the screen edge) -> current only.
	if (histUv.x < 0.0 || histUv.x > 1.0 || histUv.y < 0.0 || histUv.y > 1.0)
	{
		GIOut[px] = float4(currentGI, depthCurr);
		return;
	}

	// Reprojected accumulated GI (clamp-linear tap; .rgb = irradiance, .a = prev depth for the reject).
	const float4 histSample = GIHistoryPrev.SampleLevel(LinearSampler, histUv, 0);
	const float3 historyGI = max(histSample.rgb, 0.0);

	// Staticness: 1 at rest, 0 by ~2 px/frame of motion (velocity in UV; * OutSize -> half-res pixels).
	const float speedPixels = length(velocity * float2(OutSize));
	const float staticness = saturate(1.0 - speedPixels * 0.5);

	// Velocity-aware blend: accumulate HARD when static (toward MaxBlend) so many frames' few-ray samples
	// average out; drop to the lower base weight under motion so nothing ghosts. Mirrors the TAA resolve.
	float blend = lerp(BlendHistory, MaxBlend, staticness);

	// Depth-disocclusion rejection (#127): if the current surface's linear depth differs from the reprojected
	// history's beyond a relative threshold, the history is a DIFFERENT surface -> drive its weight to 0 so
	// the reveal falls back to this frame's trace. Smoothstep so a near-threshold edge attenuates, not pops.
	if (DepthRejectScale > 0.0)
	{
		const float linCurr = LinearizeDepth(depthCurr);
		const float linPrev = LinearizeDepth(histSample.a);
		const float rel = abs(linCurr - linPrev) / max(linCurr, 1e-4);
		const float depthConfidence = 1.0 - smoothstep(DepthRejectScale, 2.0 * DepthRejectScale, rel);
		blend *= depthConfidence;
	}

	// Pure accumulation in linear irradiance (no clamp — the à-trous next handles spatial outliers).
	const float3 accumulated = lerp(currentGI, historyGI, blend);
	GIOut[px] = float4(max(accumulated, 0.0), depthCurr);
}
