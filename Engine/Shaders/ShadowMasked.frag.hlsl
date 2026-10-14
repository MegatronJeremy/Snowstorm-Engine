// Alpha-tested shadow pass, fragment stage. Same clip expression as DefaultLit / DepthPrepass
// (albedo.a * BaseColor.a - AlphaCutoff), so the shadow silhouette matches the lit cutout.
// Paired with ShadowMasked.vert.hlsl; the push layout must match field-for-field.
struct ShadowMaskedPush
{
	float4x4 LightViewProj;
	uint AlbedoTextureIndex;
	uint AlphaMaskEnabled;
	float AlphaCutoff;
	float BaseAlpha;
};
[[vk::push_constant]] ShadowMaskedPush gShadow;

SamplerState AlbedoSampler : register(s0, space1);
Texture2D Textures[] : register(t0, space3);

struct ShadowMaskedVSOut
{
	float4 PositionCS : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

void main(ShadowMaskedVSOut input)
{
	if (gShadow.AlphaMaskEnabled != 0 && gShadow.AlbedoTextureIndex != 0)
	{
		const float alpha = Textures[NonUniformResourceIndex(gShadow.AlbedoTextureIndex)].Sample(AlbedoSampler, input.TexCoord).a * gShadow.BaseAlpha;
		clip(alpha - gShadow.AlphaCutoff);
	}
}
