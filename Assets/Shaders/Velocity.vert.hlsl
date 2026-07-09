// Motion-vector pass, vertex stage (#44). Like the depth-only shadow pass, this includes ONLY the
// minimal mesh interface (VSInput + set-2 Instances) — NOT Engine.hlsli — so it never drags FrameCB
// (set 0), the material set (set 1) or the bindless arrays (set 3) into its pipeline layout. The two
// matrices it needs travel as a 128-byte PUSH CONSTANT (the guaranteed-minimum Vulkan push size):
//   ViewProj      -- this frame's camera VP
//   PrevViewProj  -- last frame's camera VP
// Combined with the per-instance Model (this frame) and PrevModel (last frame) from set 2, the pass
// projects each vertex to clip space in BOTH frames; the fragment stage takes the screen-space delta.
// Paired with Velocity.frag.hlsl.
#include "Include/MeshInput.hlsli"

struct VelocityPush
{
	float4x4 ViewProj;
	float4x4 PrevViewProj;
};
[[vk::push_constant]] VelocityPush gVel;

struct VelocityVSOut
{
	float4 PositionCS : SV_Position; // current-frame clip pos (drives rasterization + depth)
	float4 CurrCS : TEXCOORD0;       // current-frame clip pos, passed through for the divide
	float4 PrevCS : TEXCOORD1;       // previous-frame clip pos
};

VelocityVSOut main(VSInput i, uint iid : SV_InstanceID)
{
	VelocityVSOut o;

	const float4x4 model = Instances[iid].Model;
	const float4x4 prevModel = Instances[iid].PrevModel;

	const float4 posWS = mul(float4(i.Position, 1.0), model);
	const float4 prevPosWS = mul(float4(i.Position, 1.0), prevModel);

	o.CurrCS = mul(posWS, gVel.ViewProj);
	o.PrevCS = mul(prevPosWS, gVel.PrevViewProj);
	o.PositionCS = o.CurrCS;
	return o;
}
