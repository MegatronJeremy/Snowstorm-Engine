// DDGI tile borders: each border texel copies the interior texel its octahedral position folds onto (an edge
// mirrored, a corner from the opposite corner), so bilinear taps near a tile edge read the right neighbour.
// DDGI::BorderSource in DDGIMath.hpp is the same rule, and DDGIMathTests prove it against the fold.

#include "Include/DDGICommon.hlsli"

[[vk::image_format("rgba16f")]] RWTexture2D<float4> Atlas : register(u1, space0);

[[vk::push_constant]] struct DDGIBorderPush
{
	uint Interior; // texels per tile side, border excluded
} gPush;

uint2 BorderSource(uint2 t, uint n)
{
	const bool left = t.x == 0;
	const bool right = t.x == n + 1;
	const bool top = t.y == 0;
	const bool bottom = t.y == n + 1;
	if ((left || right) && (top || bottom))
	{
		return uint2(left ? n : 1u, top ? n : 1u);
	}
	if (top || bottom)
	{
		return uint2(n + 1 - t.x, top ? 1u : n);
	}
	if (left || right)
	{
		return uint2(left ? 1u : n, n + 1 - t.y);
	}
	return t;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const DDGIVolume volume = CurrentVolume();
	const uint tile = gPush.Interior + 2;
	const uint2 tileXY = id.xy / tile;
	const uint probe = tileXY.y * volume.TilesPerRow + tileXY.x;
	if (tileXY.x >= volume.TilesPerRow || probe >= DDGIProbeCount(volume))
	{
		return;
	}
	const uint2 local = id.xy % tile;
	const uint2 source = BorderSource(local, gPush.Interior);
	if (all(source == local))
	{
		return;
	}
	Atlas[id.xy] = Atlas[tileXY * tile + source];
}
