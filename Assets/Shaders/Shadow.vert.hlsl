#include "Include/Engine.hlsli"

// Depth-only shadow pass, vertex stage. Renders scene geometry from the light's point of view into a
// depth target; the lit pass later reprojects each fragment into this space and compares depth to
// decide occlusion. Reuses the standard FrameCB (set=0) + instance buffer (set=2): the shadow pass
// binds the LIGHT's view-projection into FrameCB.ViewProj, so the same per-instance Model transforms
// produce light-space depth. Paired with Shadow.frag.hlsl (empty; depth-only, no color output).

struct ShadowVSOut
{
	float4 PositionCS : SV_Position;
};

ShadowVSOut main(VSInput i, uint iid : SV_InstanceID)
{
	ShadowVSOut o;
	const float4x4 model = Instances[iid].Model;
	const float4 posWS = mul(float4(i.Position, 1.0), model);
	o.PositionCS = mul(posWS, ViewProj); // ViewProj holds the LIGHT matrix for this pass
	return o;
}
