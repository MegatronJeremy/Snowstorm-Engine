#include "Include/Engine.hlsli"
#include "Include/UiPost.hlsli"

// One oversized triangle over the whole target, with a UV that is 0 at the target's top-left: a UI layer is
// drawn with y = -1 at its top (see Ui.vert), so its row 0 is the visual top and sampling it needs no flip.

UiPostVSOut main(uint vid : SV_VertexID)
{
	const float2 ndc = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
	UiPostVSOut o;
	o.PositionCS = float4(ndc, 0.0, 1.0);
	o.UV = ndc * 0.5 + 0.5;
	return o;
}
