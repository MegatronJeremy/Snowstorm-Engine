#pragma once

#include <cstddef>
#include <cstdint>

namespace Snowstorm
{
	// Renderer keeps one uniform ring per frame in flight and selects it by the backend's current frame
	// index. Neither value is clamped: an invalid one is a backend bug, so Renderer asserts on these.
	inline bool IsValidFramesInFlight(uint32_t framesInFlight)
	{
		return framesInFlight > 0;
	}

	inline bool IsValidFrameRingIndex(uint32_t frameIndex, size_t ringCount)
	{
		return frameIndex < ringCount;
	}
} // namespace Snowstorm
