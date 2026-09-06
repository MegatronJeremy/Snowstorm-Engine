// Sprite.hlsli - the 2D sprite pass interface shared by Sprite.vert and Sprite.frag.
//
// Set 1 (space1), bindings parked at 3/4/5 to dodge the material bindings (0/1/2) that Engine.hlsli
// declares, the same convention as the fullscreen post passes. Textures come from the bindless table
// (set 3), so one instanced draw covers every sprite regardless of how many textures they use.

#ifndef SNOWSTORM_SPRITE_HLSLI
#define SNOWSTORM_SPRITE_HLSLI

// One sprite. Mirrors SpriteInstance in Snowstorm/Render/SpriteCanvas.hpp field-for-field (64 bytes).
struct SpriteInstance
{
	float4 Rect;   // x, y, width, height in canvas units; top-left origin, y down
	float4 UVRect; // u0, v0, u1, v1
	float4 Tint;   // linear RGBA, multiplied into the sampled texel
	uint TextureIndex; // bindless index (0 = the engine's white texture)
	float Rotation;    // radians about the rect centre, clockwise on screen
	float2 _Pad;
};

// Mirrors SpriteCanvasConstants in SpriteCanvas.hpp field-for-field (two 16-byte rows).
cbuffer SpriteCB : register(b3, space1)
{
	float2 TargetSize;   // render target size in pixels
	float CanvasScale;   // canvas units -> target pixels (uniform, letterboxed)
	float _SpritePad0;
	float2 CanvasOffset; // target-pixel position of the canvas origin
	float2 _SpritePad1;
};

StructuredBuffer<SpriteInstance> Sprites : register(t4, space1);
SamplerState SpriteSampler : register(s5, space1);

struct SpriteVSOut
{
	float4 PositionCS : SV_Position;
	float2 UV : TEXCOORD0;
	float4 Tint : TEXCOORD1;
	nointerpolation uint TextureIndex : TEXCOORD2;
};

#endif // SNOWSTORM_SPRITE_HLSLI
