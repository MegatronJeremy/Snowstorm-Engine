// Depth+normal prepass, fragment stage (#124). Writes the interpolated world-space normal (.xyz,
// re-normalized) AND the NDC depth (.w = SV_Position.z, the depth-buffer value) into ONE RGBA16F color
// target. Packing depth into .w means the half-res GI compute pass (and the bilateral upsample) sample a
// single plain COLOR image — NOT the depth-stencil attachment. That matters: a depth-stencil image in a
// shader lives in DEPTH_STENCIL_READ_ONLY layout, which a compute sampled-image descriptor (expecting
// SHADER_READ_ONLY) rejects; a color image transitions to SHADER_READ_ONLY cleanly. The D32 attachment is
// still present for the prepass's own z-test (+ forward early-z), just never sampled. fp16 depth loses
// precision near the far plane, but half-res GI is coarse and the ray origin's normal-offset guards
// self-intersection. Paired with DepthNormal.vert.hlsl.
//
// Alpha-mask clip (#124): cutout geometry (glTF MASK — Sponza's plants/vines/chains) must punch holes in
// the G-buffer too, or the GI pass reconstructs phantom solid surfaces where the texture is transparent
// (wrong ray origins + wrong bilateral silhouettes). The per-material alpha fields + bindless albedo index
// ride the PUSH CONSTANT (see DepthNormal.vert), and the albedo is sampled from the bindless Textures[]
// array (set 3) with this pass's OWN sampler (set 1, binding 0) — deliberately NOT the material's
// descriptor set: that set was allocated against DefaultLit's set-1 layout, and binding it under this
// pipeline is a layout-incompatibility device loss. Sets 0 (FrameCB) + 2 (instances-in-VS) round out the
// layout; set 2 is bound for the VS's InstanceData.

struct DepthNormalPush
{
	float4x4 ViewProj;
	uint AlbedoTextureIndex;
	uint AlphaMaskEnabled;
	float AlphaCutoff;
	float BaseAlpha;
};
[[vk::push_constant]] DepthNormalPush gDN;

// This pass's own sampler (set 1, binding 0) — NOT the material set. Clamp-linear is fine for an alpha tap.
SamplerState AlbedoSampler : register(s0, space1);

// Bindless 2D textures (set 3) — the same array DefaultLit samples; the albedo index comes via the push.
Texture2D Textures[] : register(t0, space3);

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : TEXCOORD0;
	float2 TexCoord : TEXCOORD1;
};

float4 main(DepthNormalVSOut input) : SV_Target
{
	// Alpha-mask cutout: discard texels below the cutoff BEFORE writing the G-buffer, so transparent parts
	// of a cutout leaf leave a hole (no phantom normal/depth). Mirrors DefaultLit's clip(). clip() discards
	// when its argument is < 0. Only for MASK materials with an albedo texture; a no-op otherwise.
	if (gDN.AlphaMaskEnabled != 0 && gDN.AlbedoTextureIndex != 0)
	{
		const float alpha = Textures[NonUniformResourceIndex(gDN.AlbedoTextureIndex)].Sample(AlbedoSampler, input.TexCoord).a * gDN.BaseAlpha;
		clip(alpha - gDN.AlphaCutoff);
	}

	// Re-normalize: linear interpolation across the triangle shortens the normal. .w = NDC depth (0..1),
	// which SV_Position.z carries in the fragment stage — the same value the depth buffer stores.
	const float3 n = normalize(input.NormalWS);
	return float4(n, input.PositionCS.z);
}
