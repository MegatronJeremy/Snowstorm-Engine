// Bilinear upscale pass (#43): sample the low-res HDR scene target and write it into the full-res HDR
// upscale target. Pure hardware-bilinear resample — the placeholder the neural super-resolution
// upscaler later replaces in-place (same inputs/output, smarter shader). Paired with Fullscreen.vert.hlsl.
//
// HDR passthrough: NO tonemap, NO sRGB — input and output are both linear RGBA16F. The tonemap pass
// downstream still does exposure/ACES.
//
// Self-contained resources on SET 1 (space1). Sets 0 (Frame) and 2 (Object) are engine-shared and
// layout-checked across pipelines, so this pass MUST NOT use them. Like FxaaPass, the shared
// Fullscreen.vert.hlsl drags Engine.hlsli's set-1 material bindings (b0/s1/s2) into the vertex stage,
// so this pass parks its texture+sampler at high binding numbers (4/5) to avoid the merge collision.

Texture2D SceneTex : register(t4, space1);
SamplerState SceneSampler : register(s5, space1);

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0; // clip-space xy in [-1,1]
};

float4 main(FullscreenVSOut input) : SV_Target
{
	// NDC [-1,1] -> UV [0,1]. No Y flip (same orientation convention as the tonemap/FXAA passes; ImGui
	// applies its own flip at display). The low-res source is sampled with a clamp-linear sampler, so the
	// hardware bilinear filter does the actual upscale as UV steps land between low-res texels.
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);
	return float4(SceneTex.Sample(SceneSampler, uv).rgb, 1.0);
}
