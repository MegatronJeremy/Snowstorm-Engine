// FXAA 3.11 (Timothy Lottes / NVIDIA), compact quality variant — the spatial-AA baseline (#40/#82).
// Runs AFTER tonemap, sampling the tonemapped LDR image in GAMMA (sRGB) space, which is what FXAA is
// designed for (luma is perceptual). It reads the intermediate through a UNORM view (raw sRGB bytes),
// edge-blends, then converts back to LINEAR because its own present target is an sRGB-format image and
// the hardware re-encodes on write (#79). Paired with Fullscreen.vert.hlsl.
//
// Self-contained resources on SET 1 (space1). Sets 0 (Frame) and 2 (Object) are engine-shared and
// layout-checked across pipelines, so this pass MUST NOT use them.
//
// IMPORTANT: the shared Fullscreen.vert.hlsl includes Engine.hlsli, which declares set-1 material
// bindings b0 (MaterialCB), s1 (LinearSampler), s2 (ClampSampler). Compiled -Od, those survive in the
// vertex stage, so the pipeline's merged set-1 layout ALREADY has bindings 0/1/2 taken. FXAA must use
// DIFFERENT binding numbers or the merge collides (image vs sampler at the same binding). Park them high.

cbuffer FxaaCB : register(b3, space1)
{
	float2 RcpFrame; // 1.0 / viewport size (texel size in UV)
	float2 _FxaaPad;
};

Texture2D SceneTex : register(t4, space1);
SamplerState SceneSampler : register(s5, space1);

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0; // clip-space xy in [-1,1]
};

// Perceptual luma from gamma-space RGB (FXAA keys off green-weighted luma).
float FxaaLuma(float3 c)
{
	return dot(c, float3(0.299, 0.587, 0.114));
}

// Convert the gamma/sRGB result back to linear for the hardware sRGB-encode on write (#79).
float3 SrgbToLinear(float3 c)
{
	c = max(c, 0.0);
	const float3 lo = c / 12.92;
	const float3 hi = pow((c + 0.055) / 1.055, 2.4);
	return lerp(hi, lo, step(c, 0.04045));
}

// FXAA quality thresholds (Lottes defaults).
static const float kEdgeThreshold = 0.125;    // min local contrast (of max luma) to bother AA-ing
static const float kEdgeThresholdMin = 0.0312; // absolute floor so near-black regions don't shimmer

float4 main(FullscreenVSOut input) : SV_Target
{
	// NDC [-1,1] -> UV [0,1] (no Y flip: same orientation as the tonemap output; ImGui applies its flip).
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);

	// 3x3 cross + corners in gamma space.
	const float3 rgbM = SceneTex.Sample(SceneSampler, uv).rgb;
	const float lumaM = FxaaLuma(rgbM);
	const float lumaN = FxaaLuma(SceneTex.Sample(SceneSampler, uv + float2(0.0, -RcpFrame.y)).rgb);
	const float lumaS = FxaaLuma(SceneTex.Sample(SceneSampler, uv + float2(0.0, RcpFrame.y)).rgb);
	const float lumaW = FxaaLuma(SceneTex.Sample(SceneSampler, uv + float2(-RcpFrame.x, 0.0)).rgb);
	const float lumaE = FxaaLuma(SceneTex.Sample(SceneSampler, uv + float2(RcpFrame.x, 0.0)).rgb);

	const float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	const float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	const float range = lumaMax - lumaMin;

	// Flat region: not enough contrast to be an edge -> passthrough (converted to linear for HW encode).
	if (range < max(kEdgeThresholdMin, lumaMax * kEdgeThreshold))
	{
		return float4(SrgbToLinear(rgbM), 1.0);
	}

	// Edge direction from the Sobel-ish gradient of the 4-neighbourhood.
	float2 dir;
	dir.x = -((lumaN + lumaS) - (2.0 * lumaM));  // vertical gradient drives horizontal blur
	dir.y = ((lumaW + lumaE) - (2.0 * lumaM));   // horizontal gradient drives vertical blur
	// Fall back to the cardinal gradient so pure horizontal/vertical edges still get a direction.
	if (abs(dir.x) + abs(dir.y) < 1e-5)
	{
		dir = float2(lumaE - lumaW, lumaS - lumaN);
	}

	const float dirReduce = max((lumaN + lumaS + lumaW + lumaE) * 0.25 * 0.25, 1.0 / 128.0);
	const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, -8.0, 8.0) * RcpFrame;

	// Two tap pairs along the edge (inner + outer), the classic FXAA blend.
	const float3 rgbA = 0.5 * (SceneTex.Sample(SceneSampler, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
	                           SceneTex.Sample(SceneSampler, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	const float3 rgbB = rgbA * 0.5 + 0.25 * (SceneTex.Sample(SceneSampler, uv + dir * -0.5).rgb +
	                                         SceneTex.Sample(SceneSampler, uv + dir * 0.5).rgb);

	// If the wider (4-tap) blend strayed outside the local luma range, it over-blurred -> fall back to the
	// tighter 2-tap. This is the canonical FXAA3 console-quality selection.
	const float lumaB = FxaaLuma(rgbB);
	const float3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

	return float4(SrgbToLinear(result), 1.0);
}
