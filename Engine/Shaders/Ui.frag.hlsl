#include "Include/Engine.hlsli"
#include "Include/Ui.hlsli"

// Premultiplied texel times premultiplied vertex colour, blended One / OneMinusSrcAlpha. The target is the
// UNORM swapchain and UI textures are uploaded UNORM too, so colours stay in the sRGB space CSS authors them
// in and blend there, as a browser does.

float4 main(UiVSOut input) : SV_Target
{
	float4 texel = float4(1.0, 1.0, 1.0, 1.0);
	if (gUi.Flags & 1u)
	{
		texel = Textures[NonUniformResourceIndex(gUi.TextureIndex)].Sample(UiSampler, input.UV);
	}
	return texel * input.Colour;
}
