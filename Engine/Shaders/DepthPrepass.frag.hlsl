// Camera depth prepass, fragment stage (early-Z for the Forward pass). Writes ONLY depth (no color
// target) so the expensive DefaultLit forward shader runs on visible fragments instead of ~2x
// overdraw. Pairs with DepthNormal.vert.hlsl (reused as-is): the VS emits normal/tangent varyings this
// stage ignores; we consume only TexCoord for the alpha-cutout clip. Same DepthNormalPush layout +
// bindless albedo (set 3) + pass sampler (set 1) as the depth+normal prepass, so it is driven by the
// same RendererService::DrawBatchesDepthNormal call.
//
// Alpha-mask clip mirrors DefaultLit / DepthNormal exactly (same albedo index, cutoff, base alpha), so
// the prepass depth matches the forward depth on cutout geometry -- otherwise early-Z would reject
// fragments visible through a cutout hole (a phantom solid where the texture is transparent).

struct DepthNormalPush
{
	float4x4 ViewProj;
	uint AlbedoTextureIndex;
	uint AlphaMaskEnabled;
	float AlphaCutoff;
	float BaseAlpha;

	uint NormalTextureIndex;
	float Roughness;
	uint MetallicRoughnessTextureIndex;
	uint _Pad0;
};
[[vk::push_constant]] DepthNormalPush gDN;

SamplerState AlbedoSampler : register(s0, space1);
Texture2D Textures[] : register(t0, space3);

struct DepthNormalVSOut
{
	float4 PositionCS : SV_Position;
	float3 NormalWS : TEXCOORD0;
	float4 TangentWS : TEXCOORD1;
	float2 TexCoord : TEXCOORD2;
};

// Depth-only: no SV_Target. Depth is written from the rasterized SV_Position; the shader only clips.
void main(DepthNormalVSOut input)
{
	if (gDN.AlphaMaskEnabled != 0 && gDN.AlbedoTextureIndex != 0)
	{
		const float alpha = Textures[NonUniformResourceIndex(gDN.AlbedoTextureIndex)].Sample(AlbedoSampler, input.TexCoord).a * gDN.BaseAlpha;
		clip(alpha - gDN.AlphaCutoff);
	}
}
