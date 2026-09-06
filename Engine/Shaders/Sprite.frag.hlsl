#include "Include/Engine.hlsli"
#include "Include/Sprite.hlsli"

// Straight-alpha texel times tint. The pipeline blends SrcAlpha / OneMinusSrcAlpha, and the target is
// the sRGB-format present image, so the output is linear and the hardware encodes on write: sprite
// textures loaded as sRGB decode to linear on sample, which is what makes that round trip exact.

float4 main(SpriteVSOut input) : SV_Target
{
	const float4 texel = Textures[NonUniformResourceIndex(input.TextureIndex)].Sample(SpriteSampler, input.UV);
	return texel * input.Tint;
}
