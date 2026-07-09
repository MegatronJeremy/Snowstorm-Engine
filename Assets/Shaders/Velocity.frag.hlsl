// Motion-vector pass, fragment stage (#44). Takes the current + previous clip-space positions from the
// vertex stage, does the perspective divide, converts each to UV space, and writes the per-pixel
// screen-space motion (curr_uv - prev_uv) into .xy of an RGBA16F target. This is the standard temporal
// motion vector consumed by TAA / temporal upscalers (a later #44 increment reprojects history by it).
//
// UV convention: uv = ndc.xy * float2(0.5, -0.5) + 0.5 (Vulkan clip-space Y points down relative to UV,
// hence the -0.5 on Y). The +0.5 bias cancels in the delta, so only the 0.5 scale + Y sign remain. The
// result is in UV units ([0,1] across the screen), so a full-screen pan is ~1.0 and a static pixel is 0.
// .zw are unused (0). Paired with Velocity.vert.hlsl. No color output transform (raw data buffer).

struct VelocityVSOut
{
	float4 PositionCS : SV_Position;
	float4 CurrCS : TEXCOORD0;
	float4 PrevCS : TEXCOORD1;
};

float4 main(VelocityVSOut input) : SV_Target
{
	const float2 currUV = (input.CurrCS.xy / input.CurrCS.w) * float2(0.5, -0.5) + 0.5;
	const float2 prevUV = (input.PrevCS.xy / input.PrevCS.w) * float2(0.5, -0.5) + 0.5;
	const float2 velocity = currUV - prevUV;
	return float4(velocity, 0.0, 0.0);
}
