// Contrast-Adaptive Sharpening (AMD FidelityFX CAS), display-space (#44). The hue-safe home for the
// sharpen that does NOT belong in the temporal resolve: sharpening pre-tonemap in linear HDR turns any
// overshoot into a hue shift once the per-channel ACES curve runs. CAS runs AFTER tonemap on the LDR
// image (like FXAA), in perceptual/gamma space, and is adaptive — it sharpens low-contrast detail more
// and already-high-contrast edges less, so it stays within local color bounds and can't ring/halo.
//
// Reads the tonemapped LDR result through a UNORM sample view (raw gamma bytes, same as FXAA), sharpens,
// then converts back to LINEAR because the present target is sRGB-format and the hardware re-encodes on
// write (#79). Self-contained resources on SET 1 (space1), bindings parked high to dodge the material
// bindings Fullscreen.vert.hlsl drags in via Engine.hlsli. Paired with Fullscreen.vert.hlsl.

cbuffer SharpenCB : register(b3, space1)
{
	float2 RcpFrame; // 1 / viewport size (texel step)
	float Sharpness; // 0..1: CAS sharpening amount (0 = off, but the pass is gated off at 0 upstream)
	float _Pad;
};

Texture2D SceneTex : register(t4, space1);
SamplerState SceneSampler : register(s5, space1);

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

// Gamma/sRGB -> linear, for the hardware sRGB-encode on write (mirrors Fxaa.frag.hlsl).
float3 SrgbToLinear(float3 c)
{
	c = max(c, 0.0);
	const float3 lo = c / 12.92;
	const float3 hi = pow((c + 0.055) / 1.055, 2.4);
	return lerp(hi, lo, step(c, 0.04045));
}

float3 Tap(float2 uv, float2 off)
{
	return SceneTex.Sample(SceneSampler, uv + off * RcpFrame).rgb;
}

float4 main(FullscreenVSOut input) : SV_Target
{
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);

	// 3x3 neighborhood (gamma space):
	//   a b c
	//   d e f
	//   g h i
	const float3 a = Tap(uv, float2(-1, -1));
	const float3 b = Tap(uv, float2(0, -1));
	const float3 c = Tap(uv, float2(1, -1));
	const float3 d = Tap(uv, float2(-1, 0));
	const float3 e = Tap(uv, float2(0, 0));
	const float3 f = Tap(uv, float2(1, 0));
	const float3 g = Tap(uv, float2(-1, 1));
	const float3 h = Tap(uv, float2(0, 1));
	const float3 i = Tap(uv, float2(1, 1));

	// CAS: per-channel local min/max of the cross, then include the corners (the "soft" min/max), which
	// makes the amount adaptive to local contrast.
	float3 mn = min(min(min(d, e), min(f, b)), h);
	mn += min(mn, min(min(a, c), min(g, i)));
	float3 mx = max(max(max(d, e), max(f, b)), h);
	mx += max(mx, max(max(a, c), max(g, i)));

	// Sharpening amount per channel: strong where there's headroom, gentle near clipped values. sqrt gives
	// CAS its characteristic falloff. amp in [0,1].
	const float3 rcpMx = rcp(max(mx, 1e-4));
	float3 amp = saturate(min(mn, 2.0 - mx) * rcpMx);
	amp = sqrt(amp);

	// Peak negative lobe of the sharpening kernel, from -1/8 (subtle) to -1/5 (strong) by Sharpness.
	const float peak = -rcp(lerp(8.0, 5.0, saturate(Sharpness)));
	const float3 w = amp * peak;

	// Normalized weighted blend of the 4-neighbour cross with the centre — a controlled unsharp that stays
	// within the local range (adaptive, so no ringing). Per-channel weight, but display-space + bounded, so
	// hue stays put (unlike a pre-tonemap linear sharpen).
	const float3 rcpWeight = rcp(1.0 + 4.0 * w);
	const float3 sharpened = (w * (b + d + f + h) + e) * rcpWeight;

	return float4(SrgbToLinear(sharpened), 1.0);
}
