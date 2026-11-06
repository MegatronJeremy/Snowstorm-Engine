// Sprite pass, vertex stage. Standalone: it includes no Engine.hlsli, so the pipeline layout is this pass's
// set 1 plus the bindless set 3, and never the shared frame/object sets. Six vertices per instance, no
// vertex buffer. Paired with Sprite.frag.hlsl.

// MUST match SpriteInstance in Snowstorm/Render/SpriteBatch.hpp.
struct SpriteInstance
{
	float4x4 Model; // unit quad [0,1]^2 to world, size/pivot/flips baked in
	float4 Tint;
	uint TextureIndex;
	uint Flags;
	uint2 _Pad;
	float4 UvRect; // xy offset, zw scale
};

StructuredBuffer<SpriteInstance> Instances : register(t0, space1);

// BaseInstance: several viewports share one per-frame instance buffer, and SV_InstanceID starts at 0.
struct SpritePush
{
	float4x4 ViewProjection;
	uint BaseInstance;
};
[[vk::push_constant]] SpritePush gSprite;

struct SpriteVSOut
{
	float4 PositionCS : SV_Position;
	float2 UV : TEXCOORD0;
	nointerpolation float4 Tint : TEXCOORD1;
	nointerpolation uint TextureIndex : TEXCOORD2;
	nointerpolation uint Flags : TEXCOORD3;
};

static const float2 kCorners[6] = {
	float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
	float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};

SpriteVSOut main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	const SpriteInstance inst = Instances[gSprite.BaseInstance + instanceId];
	const float2 corner = kCorners[vertexId];

	SpriteVSOut o;
	o.PositionCS = mul(mul(float4(corner, 0.0, 1.0), inst.Model), gSprite.ViewProjection);
	// Texture rows are stored top-down, so the quad's top edge samples v = 0 of the cell.
	o.UV = inst.UvRect.xy + float2(corner.x, 1.0 - corner.y) * inst.UvRect.zw;
	o.Tint = inst.Tint;
	o.TextureIndex = inst.TextureIndex;
	o.Flags = inst.Flags;
	return o;
}
