// Depth-only pass: include ONLY the minimal mesh interface (VSInput + set-2 Instances), not the full
// Engine.hlsli. Pulling in Engine.hlsli dragged FrameCB (set 0), MaterialCB (set 1) and the bindless
// arrays (set 3) into this shader's SPIR-V, so a debugger flagged those sets as referenced-but-unbound
// on every shadow draw even though this pass only binds set 2.
#include "Include/MeshInput.hlsli"

// Depth-only shadow pass, vertex stage. Renders scene geometry from the light's point of view into a
// depth target; the lit pass later reprojects each fragment into this space and compares depth to
// decide occlusion. Uses the instance buffer (set=2) for per-object Model transforms and takes the
// light's view-projection via a PUSH CONSTANT (not FrameCB): one command buffer renders many light
// views (the sun + each shadow-casting spot's atlas tile), and FrameCB is cached one-per-pipeline so it
// can't carry a different matrix per draw-set. A push constant is per-draw, so each view pushes its own.
// Paired with Shadow.frag.hlsl (empty; depth-only, no color output).

struct ShadowPush
{
	float4x4 LightViewProj;
};
[[vk::push_constant]] ShadowPush gShadow;

struct ShadowVSOut
{
	float4 PositionCS : SV_Position;
};

ShadowVSOut main(VSInput i, uint iid : SV_InstanceID)
{
	ShadowVSOut o;
	const float4x4 model = Instances[iid].Model;
	const float4 posWS = mul(float4(i.Position, 1.0), model);
	o.PositionCS = mul(posWS, gShadow.LightViewProj);
	return o;
}
