#include "Engine.hlsli"

// Fullscreen-triangle vertex stage: emits one oversized triangle covering the whole screen from
// SV_VertexID alone (no vertex buffer). Shared by any fullscreen pass — the procedural sky today, and
// the natural VS for a future post-process pass (#53). Outputs clip-space NDC so the fragment can
// reconstruct a view ray (sky) or sample by screen UV (post). z = w => NDC z = 1.0 (far plane), so a
// LessOrEqual depth test keeps a sky fragment behind already-drawn geometry.

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0; // clip-space xy in [-1,1]
};

FullscreenVSOut main(uint vid : SV_VertexID)
{
	FullscreenVSOut o;
	const float2 ndc = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
	o.NDC = ndc;
	o.PositionCS = float4(ndc, 1.0, 1.0);
	return o;
}
