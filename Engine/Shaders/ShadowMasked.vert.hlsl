// Alpha-tested shadow pass, vertex stage: the MASK-material variant of Shadow.vert.hlsl. Adds the UV
// varying and per-batch cutout scalars so the fragment stage can clip; opaque geometry keeps using the
// position-only Shadow.vert/frag pair, which references set 2 alone.
#include "Include/MeshInput.hlsli"

struct ShadowMaskedPush
{
	float4x4 LightViewProj;
	uint AlbedoTextureIndex; // bindless index (0 = no albedo -> no clip)
	uint AlphaMaskEnabled;
	float AlphaCutoff;
	float BaseAlpha;
};
[[vk::push_constant]] ShadowMaskedPush gShadow;

struct ShadowMaskedVSOut
{
	float4 PositionCS : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

ShadowMaskedVSOut main(VSInput i, uint iid : SV_InstanceID)
{
	ShadowMaskedVSOut o;
	const float4 posWS = mul(float4(i.Position, 1.0), Instances[iid].Model);
	o.PositionCS = mul(posWS, gShadow.LightViewProj);
	o.TexCoord = i.TexCoord;
	return o;
}
