#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct ShadowAtlasTile
	{
		uint32_t X = 0, Y = 0, Size = 0; // pixels
	};

	// Tile `tile` of a square atlas `cols` tiles wide, row-major, each `tilePx` pixels.
	[[nodiscard]] constexpr ShadowAtlasTile ShadowAtlasTileRect(const uint32_t tile, const uint32_t cols, const uint32_t tilePx)
	{
		return {(tile % cols) * tilePx, (tile / cols) * tilePx, tilePx};
	}

	// Secondary directional with ShadowSlot s >= 1 renders into tile s - 1 of the directional atlas.
	[[nodiscard]] constexpr uint32_t DirectionalAtlasTile(const int shadowSlot)
	{
		return static_cast<uint32_t>(shadowSlot - 1);
	}

	// A point's six cube faces occupy consecutive tiles: slot * 6 + face.
	[[nodiscard]] constexpr uint32_t PointAtlasTile(const int shadowSlot, const int face)
	{
		return static_cast<uint32_t>(shadowSlot * 6 + face);
	}
}
