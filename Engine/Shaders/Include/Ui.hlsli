// Ui.hlsli - the UI pass interface shared by Ui.vert and Ui.frag (Snowstorm/UI, RmlUi's geometry).
//
// Set 1 (space1), bindings parked at 4/5/6 like the sprite pass, clear of the material bindings Engine.hlsli
// declares. Textures come from the bindless table (set 3). Everything that changes per draw rides in the push
// constant, so one pipeline and one descriptor set cover a whole frame of UI.

#ifndef SNOWSTORM_UI_HLSLI
#define SNOWSTORM_UI_HLSLI

// RmlUi's Vertex, five 32-bit words each and read raw so no packing rule can disagree with the C++ side:
// position.xy (float), colour (RGBA8, premultiplied alpha, R in the low byte), tex_coord.xy (float).
StructuredBuffer<uint> UiVertices : register(t4, space1);
SamplerState UiSampler : register(s5, space1);

// A compiled RmlUi gradient (linear-, radial- or conic-gradient, repeating or not). Mirrors UiGradientGpu in
// UiRenderer.cpp field-for-field (352 bytes).
#define UI_GRADIENT_LINEAR 0
#define UI_GRADIENT_RADIAL 1
#define UI_GRADIENT_CONIC 2
#define UI_GRADIENT_MAX_STOPS 16
struct UiGradient
{
	uint Kind;
	uint Repeating;
	uint StopCount;
	float Angle;    // conic: the start angle, radians clockwise from the top
	float2 P0;      // linear: the start point; radial and conic: the centre
	float2 P1;      // linear: the end point; radial: the radii
	float4 Colors[UI_GRADIENT_MAX_STOPS]; // premultiplied
	float4 Positions[UI_GRADIENT_MAX_STOPS / 4]; // along the gradient line, 0 at P0 and 1 at P1
};
StructuredBuffer<UiGradient> UiGradients : register(t6, space1);

#define UI_FLAG_TEXTURED 1u
#define UI_FLAG_GRADIENT 2u
#define UI_FLAG_MASK_WRITE 4u // writing a clip mask: MaskValue everywhere the geometry covers, no blending

// Mirrors UiPushConstants in UiRenderer.cpp field-for-field (104 bytes).
struct UiPush
{
	// projection * RmlUi's element transform, as four columns (column-major, as Rml::Matrix4f stores it).
	float4 Col0;
	float4 Col1;
	float4 Col2;
	float4 Col3;
	float2 Translation; // RmlUi's per-draw offset, in pixels, applied before the transform
	uint TextureIndex;  // bindless index; ignored when untextured
	uint Flags;         // UI_FLAG_*
	uint GradientIndex; // into UiGradients, with UI_FLAG_GRADIENT
	uint MaskIndex;     // bindless index of the clip mask in force, 0 for none: coverage is its red channel
	float MaskValue;    // with UI_FLAG_MASK_WRITE
	uint _Pad;
	// Texture coordinates are multiplied by this, and what lands past 1 is transparent. A box-shadow RmlUi baked
	// while the window was smaller than it is clipped to the window, yet drawn on a quad of the size it asked for:
	// this draws the part that exists at 1:1 instead of stretching it over the whole quad. (1, 1) otherwise.
	float2 UvScale;
};

[[vk::push_constant]] UiPush gUi;

struct UiVSOut
{
	float4 PositionCS : SV_Position;
	float4 Colour : COLOR0; // premultiplied
	float2 UV : TEXCOORD0;
	float2 Local : TEXCOORD1; // the vertex's own position, before translation: where a gradient is evaluated
};

#endif // SNOWSTORM_UI_HLSLI
