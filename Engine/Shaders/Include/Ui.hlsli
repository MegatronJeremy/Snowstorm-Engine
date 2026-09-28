// Ui.hlsli - the UI pass interface shared by Ui.vert and Ui.frag (Snowstorm/UI, RmlUi's geometry).
//
// Set 1 (space1), bindings parked at 4/5 like the sprite pass, clear of the material bindings Engine.hlsli
// declares. Textures come from the bindless table (set 3). Everything that changes per draw rides in the push
// constant, so one pipeline and one descriptor set cover a whole frame of UI.

#ifndef SNOWSTORM_UI_HLSLI
#define SNOWSTORM_UI_HLSLI

// RmlUi's Vertex, five 32-bit words each and read raw so no packing rule can disagree with the C++ side:
// position.xy (float), colour (RGBA8, premultiplied alpha, R in the low byte), tex_coord.xy (float).
StructuredBuffer<uint> UiVertices : register(t4, space1);
SamplerState UiSampler : register(s5, space1);

struct UiPush
{
	// projection * RmlUi's element transform, as four columns (column-major, as Rml::Matrix4f stores it).
	float4 Col0;
	float4 Col1;
	float4 Col2;
	float4 Col3;
	float2 Translation; // RmlUi's per-draw offset, in pixels, applied before the transform
	uint TextureIndex;  // bindless index; ignored when untextured
	uint Flags;         // bit 0: sample TextureIndex
};

[[vk::push_constant]] UiPush gUi;

struct UiVSOut
{
	float4 PositionCS : SV_Position;
	float4 Colour : COLOR0; // premultiplied
	float2 UV : TEXCOORD0;
};

#endif // SNOWSTORM_UI_HLSLI
