// Runtime present pass (#4): copy the primary viewport's final LDR image to the swapchain. The Runtime
// has no ImGui backend, so the editor's ImGui swapchain pass never runs and nothing composes the
// swapchain (blank window). This is the non-ImGui equivalent: a fullscreen triangle that samples the
// already-tonemapped present image and writes it straight to the swapchain.
//
// NO color conversion. SceneTex is the present target's UNORM sample view (raw gamma-encoded bytes, the
// same view ImGui samples), and the swapchain is a UNORM format (B8G8R8A8_UNORM) — so the bytes pass
// through unchanged, exactly like the editor's ImGui blit. (Contrast Sharpen/Fxaa, which SrgbToLinear on
// write because THEIR target is an sRGB-format image the hardware re-encodes; the swapchain is not sRGB,
// so converting here would double-darken.)
//
// Self-contained resources on SET 1 (space1), bindings parked high (t4/s5) to dodge the material
// bindings (0/1/2) that Fullscreen.vert.hlsl drags in via Engine.hlsli — same convention as Sharpen/Fxaa.
// Paired with Fullscreen.vert.hlsl. No cbuffer: a pure copy takes no parameters.

Texture2D SceneTex : register(t4, space1);
SamplerState SceneSampler : register(s5, space1);

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

float4 main(FullscreenVSOut input) : SV_Target
{
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);
	return float4(SceneTex.Sample(SceneSampler, uv).rgb, 1.0);
}
