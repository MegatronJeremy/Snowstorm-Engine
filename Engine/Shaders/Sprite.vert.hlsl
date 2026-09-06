#include "Include/Engine.hlsli"
#include "Include/Sprite.hlsli"

// Instanced screen-space quads: six vertices per instance from SV_VertexID (no vertex buffer), one
// instance per SpriteInstance. Canvas space is top-left origin, y down, in design units; the C++ side
// fits the canvas into the target with a uniform scale and a centring offset (letterbox).
//
// NDC y = +1 is the visual top here. The engine renders in un-flipped Vulkan clip space with a GL-style
// projection, so the present target's row 0 is the visual bottom (the runtime present pass inverts V
// for the same reason). A sprite drawn with y = +1 at its top therefore lands upright in that image.

SpriteVSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	const SpriteInstance s = Sprites[iid];

	// Two triangles over corners (0,0) (1,0) (0,1) (1,1): 0 1 2, 2 1 3.
	const uint corner = (vid < 3) ? vid : ((vid == 3) ? 2 : ((vid == 4) ? 1 : 3));
	const float2 unit = float2(corner & 1, corner >> 1);

	const float2 local = (unit - 0.5) * s.Rect.zw;
	const float c = cos(s.Rotation);
	const float sn = sin(s.Rotation);
	const float2 rotated = float2(local.x * c - local.y * sn, local.x * sn + local.y * c);
	const float2 canvasPos = s.Rect.xy + s.Rect.zw * 0.5 + rotated;

	const float2 px = CanvasOffset + canvasPos * CanvasScale;
	const float2 ndc = float2(px.x / TargetSize.x * 2.0 - 1.0, 1.0 - px.y / TargetSize.y * 2.0);

	SpriteVSOut o;
	o.PositionCS = float4(ndc, 0.0, 1.0);
	o.UV = lerp(s.UVRect.xy, s.UVRect.zw, unit);
	o.Tint = s.Tint;
	o.TextureIndex = s.TextureIndex;
	return o;
}
