// Temporal resolve / TAA (#44). The payoff pass for jitter + motion vectors: reproject last frame's
// resolved output by the per-pixel velocity, blend it with the current jittered frame, and write the
// result — which both feeds tonemap AND becomes next frame's history. Over many frames this accumulates
// the differently-jittered samples into a stable, anti-aliased (and, at internal res, sharper) image.
// This is classical TAA (Karis "high-quality temporal supersampling"); the neural upscaler later
// replaces this shader at the same seam.
//
// Runs in HDR/linear space, AFTER forward(+upscale), BEFORE tonemap. Resources mirror FxaaPass: self-
// contained on SET 1 (space1), bindings parked high (3/4/5/6) to dodge the material bindings (0/1/2)
// that Fullscreen.vert.hlsl drags in via Engine.hlsli. All three inputs are the SAME full resolution,
// so current + velocity use integer Load(); history uses a clamp-linear Sample() at the reprojected UV.

cbuffer ResolveCB : register(b3, space1)
{
	float2 RcpFrame;    // 1 / render size (texel step for neighborhood taps + UV conversion)
	float HistoryValid; // 1 = blend history, 0 = first frame / reset -> output current only
	float BlendHistory; // BASE history weight (used at speed) — the render.taa.blend CVar
	float MaxBlend;     // history weight when the pixel is ~static: deeper accumulation to kill specular
	                    // shimmer (a static camera still jitters, so shiny surfaces alias frame-to-frame)
	float3 _Pad;
};

Texture2D CurrentTex : register(t4, space1);     // current-frame HDR color (jittered)
Texture2D HistoryTex : register(t5, space1);     // previous resolved HDR (reprojected)
Texture2D VelocityTex : register(t6, space1);    // screen-space motion (.xy = curr_uv - prev_uv)
SamplerState LinearClamp : register(s7, space1); // clamp-linear; parked high to dodge material s1/s2

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

// RGB <-> YCoCg. History rejection is done in YCoCg (luma + two chroma) rather than RGB because the box
// then aligns with luminance — the axis the eye is most sensitive to — so the clip is tighter and rejects
// stale color without the chroma bleeding/ghosting an RGB min/max box lets through. Standard TAA practice.
float3 RgbToYCoCg(float3 c)
{
	return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
	              0.5 * c.r - 0.5 * c.b,
	              -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 YCoCgToRgb(float3 c)
{
	const float t = c.x - c.z;
	return float3(t + c.y, c.x + c.z, t - c.y);
}

// Bicubic Catmull-Rom history sample. History is re-sampled at a reprojected UV every frame; a plain
// bilinear tap re-blurs it each time, and under motion (UV lands between texels) that softening compounds
// -> the classic "TAA is blurry in motion". Catmull-Rom is a sharpening reconstruction filter (negative
// lobes), so it counteracts the resampling blur AT THE SOURCE instead of re-sharpening after the fact —
// the standard Unreal/Unity fix. This is the well-known 9-sample-collapsed-to-5-bilinear-fetch form
// (Filmic/Karis): the 4 corners of the 4x4 kernel vanish, leaving 5 weighted bilinear taps.
float3 SampleHistoryCatmullRom(Texture2D tex, SamplerState samp, float2 uv, float2 texSize)
{
	const float2 samplePos = uv * texSize;
	const float2 texPos1 = floor(samplePos - 0.5) + 0.5;
	const float2 f = samplePos - texPos1;

	// Cubic (Catmull-Rom) weights for the 4 taps along each axis.
	const float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	const float2 w3 = f * f * (-0.5 + 0.5 * f);

	// Combine the middle two taps into one bilinear fetch (the trick that turns 4x4 -> 3x3 fetches).
	const float2 w12 = w1 + w2;
	const float2 offset12 = w2 / w12;

	const float2 rcpTex = 1.0 / texSize;
	const float2 p0 = (texPos1 - 1.0) * rcpTex;
	const float2 p3 = (texPos1 + 2.0) * rcpTex;
	const float2 p12 = (texPos1 + offset12) * rcpTex;

	// 5 bilinear taps. Dropping the 4x4 kernel's corners for speed is standard, BUT their combined weight is
	// net-POSITIVE, so the 5 remaining weights sum to < 1 -> the sample comes back dim (zero error at a texel
	// centre, worst mid-texel -> "dims only while moving"). So accumulate the actual weights and NORMALIZE,
	// which restores energy preservation regardless of the dropped taps.
	float3 result = 0.0;
	float weightSum = 0.0;
	float w;
	w = w12.x * w0.y;  result += tex.SampleLevel(samp, float2(p12.x, p0.y), 0).rgb * w; weightSum += w;
	w = w0.x * w12.y;  result += tex.SampleLevel(samp, float2(p0.x, p12.y), 0).rgb * w; weightSum += w;
	w = w12.x * w12.y; result += tex.SampleLevel(samp, float2(p12.x, p12.y), 0).rgb * w; weightSum += w;
	w = w3.x * w12.y;  result += tex.SampleLevel(samp, float2(p3.x, p12.y), 0).rgb * w; weightSum += w;
	w = w12.x * w3.y;  result += tex.SampleLevel(samp, float2(p12.x, p3.y), 0).rgb * w; weightSum += w;
	return result / max(weightSum, 1e-5);
}

// Tonemap-space weighting (Karis, "High-Quality Temporal Supersampling"). The neighborhood clamp is done
// on RANGE-COMPRESSED color: w = 1/(1+luma) pulls bright HDR values toward [0,1] before clamp+blend, then
// the inverse expands afterwards. Without this, a bright specular sample (metallic trim, highlights) makes
// the current-frame clamp box enormous AND move every jittered frame, so the converged history lands
// outside it and is rejected -> that pixel never accumulates -> shimmer. Weighting stops the bright
// outliers from dominating, so the box stabilizes and history accumulates. Luma from the YCoCg .x channel.
float3 TonemapWeight(float3 rgb)
{
	return rgb / (1.0 + max(rgb.r, max(rgb.g, rgb.b)));
}

float3 TonemapWeightInverse(float3 rgb)
{
	return rgb / max(1.0 - max(rgb.r, max(rgb.g, rgb.b)), 1e-4);
}

// Clip `history` toward `center` so it lands inside the AABB [cmin, cmax] (all in YCoCg). Clipping toward
// the box centre along the history->centre ray (vs. a naive per-channel clamp) keeps the resolved color on
// the line to a plausible current value, which preserves more valid history than an axis-aligned clamp.
float3 ClipToAABB(float3 history, float3 center, float3 cmin, float3 cmax)
{
	const float3 halfSize = 0.5 * (cmax - cmin) + 1e-5;
	const float3 offset = history - center;
	const float3 ratio = abs(offset) / halfSize;
	const float maxRatio = max(ratio.x, max(ratio.y, ratio.z));
	if (maxRatio > 1.0)
	{
		return center + offset / maxRatio; // pull back onto the box surface
	}
	return history; // already inside
}

float4 main(FullscreenVSOut input) : SV_Target
{
	const int2 texel = int2(input.PositionCS.xy);
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);

	const float3 currentHDR = CurrentTex.Load(int3(texel, 0)).rgb;

	// First frame / history invalid: nothing to blend, output current.
	if (HistoryValid < 0.5)
	{
		return float4(currentHDR, 1.0);
	}

	// Reproject: where was this pixel last frame? velocity = curr_uv - prev_uv, so prev_uv = uv - velocity.
	const float2 velocity = VelocityTex.Load(int3(texel, 0)).xy;
	const float2 histUv = uv - velocity;

	// Off-screen history can't be trusted (disocclusion at the screen edge) -> fall back to current.
	if (histUv.x < 0.0 || histUv.x > 1.0 || histUv.y < 0.0 || histUv.y > 1.0)
	{
		return float4(currentHDR, 1.0);
	}

	// Catmull-Rom (sharpening bicubic) history reconstruction — counters the compounding bilinear-resample
	// blur that makes plain TAA soft in motion. texSize = 1/RcpFrame. max() guards a rare negative from the
	// filter's negative lobes on a high-contrast edge (would otherwise feed a NaN through the range-expand).
	const float3 historyHDR = max(SampleHistoryCatmullRom(HistoryTex, LinearClamp, histUv, 1.0 / RcpFrame), 0.0);

	// Everything below runs in TONEMAP-WEIGHTED space (Karis): range-compress before clamp+blend so bright
	// specular/HDR samples don't blow up (and jitter-shift) the clamp box and get the converged history
	// rejected — the exact cause of the shimmer on the curtain's metallic trim. Inverted at the very end.
	const float3 current = TonemapWeight(currentHDR);
	const float3 currentYCoCg = RgbToYCoCg(current);

	// Weighted-YCoCg first/second moments over the 3x3 CURRENT neighborhood -> the variance clamp box (in
	// the firefly-suppressing tonemap-weighted space, so a single bright neighbor doesn't inflate it).
	float3 m1 = 0.0; // sum of weighted-YCoCg
	float3 m2 = 0.0; // sum of weighted-YCoCg^2
	[unroll] for (int dy = -1; dy <= 1; ++dy)
	{
		[unroll] for (int dx = -1; dx <= 1; ++dx)
		{
			const float3 s = RgbToYCoCg(TonemapWeight(CurrentTex.Load(int3(texel + int2(dx, dy), 0)).rgb));
			m1 += s;
			m2 += s * s;
		}
	}
	const float3 mean = m1 / 9.0;
	const float3 variance = max(m2 / 9.0 - mean * mean, 0.0);
	const float3 stddev = sqrt(variance);

	// Staticness: 1 at rest, 0 by ~2 px/frame of motion. (velocity is in UV; /RcpFrame -> pixels.)
	const float speedPixels = length(velocity / RcpFrame);
	const float staticness = saturate(1.0 - speedPixels * 0.5);

	// VELOCITY-AWARE CLAMP WIDTH — the actual fix for the static specular shimmer. The neighborhood clamp
	// is what rejected the smooth accumulated history on the shiny curtain trim: with a still camera, jitter
	// makes each frame sample a different sub-pixel highlight, so the tight box centers on this frame's peak
	// and clips the (dimmer, averaged) history out every frame. But a static pixel has NO ghosting risk
	// (nothing moved), so we widen the box toward "accept anything" at rest and only tighten it under motion
	// (where stale history must be rejected). This is exactly what the diagnostic no-clamp probe did, made
	// conditional on motion. gamma: 1 std-dev while moving -> ~10 (effectively no clamp) at rest.
	const float gamma = lerp(1.0, 10.0, staticness);
	const float3 boxMin = mean - gamma * stddev;
	const float3 boxMax = mean + gamma * stddev;

	const float3 historyYCoCg = RgbToYCoCg(TonemapWeight(historyHDR));
	const float3 clipped = ClipToAABB(historyYCoCg, mean, boxMin, boxMax);

	// Velocity-aware blend weight: accumulate HARD when static (toward MaxBlend) so the many jittered
	// samples average out; use the lower BASE weight under motion so nothing ghosts.
	const float blend = lerp(BlendHistory, MaxBlend, staticness);

	// Accumulate in weighted YCoCg, back to weighted RGB, then EXPAND to real HDR. The blend/clamp run in
	// tonemap-compressed [0,1) space (that's what stabilizes them). This pass is PURE accumulation in linear
	// HDR — no sharpening here. A sharpen must not touch the pre-tonemap linear signal: an overshoot that's a
	// neutral brightness change in linear becomes a HUE shift once the per-channel ACES tonemap curves each
	// channel. Sharpening is a display-space op and lives in a post-tonemap pass (SharpenPass, like FXAA).
	const float3 resolvedYCoCg = lerp(currentYCoCg, clipped, blend);
	const float3 resolvedHDR = TonemapWeightInverse(clamp(YCoCgToRgb(resolvedYCoCg), 0.0, 0.999));
	return float4(max(resolvedHDR, 0.0), 1.0);
}
