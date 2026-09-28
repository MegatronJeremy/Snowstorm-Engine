#include "Include/Engine.hlsli"
#include "Include/Ui.hlsli"

// RmlUi geometry, pulled from a storage buffer by vertex index (no vertex input layout). The indices are
// rebased on the CPU, so SV_VertexID is the vertex's position in this frame's buffer.
//
// The transform maps RmlUi's pixel space (top-left origin, y down) to Vulkan clip space, whose y = -1 is the
// top of the target, so nothing is flipped. The ortho projection leaves z at 0 for untransformed geometry; it
// is folded from GL's [-w, w] into Vulkan's [0, w] so a perspective transform is not clipped.

UiVSOut main(uint vid : SV_VertexID)
{
	const uint base = vid * 5;
	const float2 position = float2(asfloat(UiVertices[base + 0]), asfloat(UiVertices[base + 1]));
	const uint colour = UiVertices[base + 2];
	const float2 uv = float2(asfloat(UiVertices[base + 3]), asfloat(UiVertices[base + 4]));

	const float2 p = position + gUi.Translation;
	float4 clip = gUi.Col0 * p.x + gUi.Col1 * p.y + gUi.Col3;
	clip.z = clip.z * 0.5 + clip.w * 0.5;

	UiVSOut o;
	o.PositionCS = clip;
	o.Colour = float4(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF, colour >> 24) / 255.0;
	o.UV = uv;
	o.Local = position;
	return o;
}
