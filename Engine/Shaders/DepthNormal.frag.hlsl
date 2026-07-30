// Depth+normal prepass, fragment stage (#124). Writes the interpolated world-space normal into an
// RGBA16F color target (.xyz = normal, re-normalized after interpolation; .w unused). Depth is written
// by the hardware from SV_Position — the target's D32 depth attachment is what the GI pass samples to
// reconstruct world position (depth + InvViewProj). No shading, no material sampling: this is a
// geometry-only prepass, so it stays cheap and also warms early-z for the forward pass that follows.
// Paired with DepthNormal.vert.hlsl. Raw data buffer — no color/output transform.

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : TEXCOORD0;
};

float4 main(DepthNormalVSOut input) : SV_Target
{
	// Re-normalize: linear interpolation across the triangle shortens the normal.
	const float3 n = normalize(input.NormalWS);
	return float4(n, 1.0);
}
