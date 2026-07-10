// Motion-vector pass, fragment stage (#44). Takes the current + previous clip-space positions from the
// vertex stage, does the perspective divide, converts each to UV space, and writes the per-pixel
// screen-space motion (curr_uv - prev_uv) into .xy of an RGBA16F target. This is the standard temporal
// motion vector consumed by TAA / temporal upscalers (a later #44 increment reprojects history by it).
//
// UV convention: uv = ndc.xy * 0.5 + 0.5 (V=0 at top) — the SAME convention every other fullscreen pass
// uses (Upscale/Fxaa/Tonemap/TemporalResolve). This MUST match the consumer: TemporalResolve reprojects
// history as histUv = uv - velocity in top-left UV space, so the velocity stored here has to be in that
// same space. (An earlier -0.5 Y flip here negated only the Y term, so vertical reprojection went the
// wrong way -> ghosting/smear on vertical motion; invisible in the abs()-based motion-vector debug view.)
// The +0.5 bias cancels in the curr-prev delta, so only the 0.5 scale + Y sign actually matter. .zw
// unused (0). Paired with Velocity.vert.hlsl. No color output transform (raw data buffer).

struct VelocityVSOut
{
	float4 PositionCS : SV_Position;
	float4 CurrCS : TEXCOORD0;
	float4 PrevCS : TEXCOORD1;
};

float4 main(VelocityVSOut input) : SV_Target
{
	const float2 currUV = (input.CurrCS.xy / input.CurrCS.w) * 0.5 + 0.5;
	const float2 prevUV = (input.PrevCS.xy / input.PrevCS.w) * 0.5 + 0.5;
	const float2 velocity = currUV - prevUV;
	return float4(velocity, 0.0, 0.0);
}
