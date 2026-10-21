// Sprite pass, fragment stage. Unlit: the texel times the tint, blended straight-alpha over what is behind.
// Paired with Sprite.vert.hlsl.

SamplerState PointSampler : register(s1, space1);
SamplerState LinearSampler : register(s2, space1);

Texture2D Textures[] : register(t0, space3);

static const uint kSpriteFlagLinear = 1u;

struct SpriteVSOut
{
	float4 PositionCS : SV_Position;
	float2 UV : TEXCOORD0;
	nointerpolation float4 Tint : TEXCOORD1;
	nointerpolation uint TextureIndex : TEXCOORD2;
	nointerpolation uint Flags : TEXCOORD3;
};

float4 main(SpriteVSOut i) : SV_Target0
{
	// One draw covers sprites with different textures, so the index is not dynamically uniform.
	const Texture2D tex = Textures[NonUniformResourceIndex(i.TextureIndex)];
	const float4 texel = (i.Flags & kSpriteFlagLinear) != 0 ? tex.Sample(LinearSampler, i.UV)
	                                                         : tex.Sample(PointSampler, i.UV);
	return texel * i.Tint;
}
