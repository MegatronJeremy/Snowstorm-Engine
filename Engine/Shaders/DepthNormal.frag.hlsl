// Depth+normal prepass, fragment stage (#124). Writes the interpolated world-space normal (.xyz,
// re-normalized) AND the NDC depth (.w = SV_Position.z, the depth-buffer value) into ONE RGBA16F color
// target. Packing depth into .w means the half-res GI compute pass (and the bilateral upsample) sample a
// single plain COLOR image — NOT the depth-stencil attachment. That matters: a depth-stencil image in a
// shader lives in DEPTH_STENCIL_READ_ONLY layout, which a compute sampled-image descriptor (expecting
// SHADER_READ_ONLY) rejects; a color image transitions to SHADER_READ_ONLY cleanly. The D32 attachment is
// still present for the prepass's own z-test (+ forward early-z), just never sampled. fp16 depth loses
// precision near the far plane, but half-res GI is coarse and the ray origin's normal-offset guards
// self-intersection. No shading, no material sampling — geometry-only. Paired with DepthNormal.vert.hlsl.

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : TEXCOORD0;
};

float4 main(DepthNormalVSOut input) : SV_Target
{
	// Re-normalize: linear interpolation across the triangle shortens the normal. .w = NDC depth (0..1),
	// which SV_Position.z carries in the fragment stage — the same value the depth buffer stores.
	const float3 n = normalize(input.NormalWS);
	return float4(n, input.PositionCS.z);
}
