#pragma once

#include <cstdint>

namespace Snowstorm::FrameRing
{
	// One ring per frame in flight; a frame index addresses a ring only while it is below the ring count.
	constexpr bool IsValidSlot(uint32_t frameIndex, uint64_t ringCount)
	{
		return frameIndex < ringCount;
	}

	// 0 frames in flight is a backend bug; the modulo would divide by zero, so it stays on slot 0.
	constexpr uint32_t NextFrameIndex(uint32_t frameIndex, uint32_t framesInFlight)
	{
		return framesInFlight == 0 ? 0u : (frameIndex + 1u) % framesInFlight;
	}
}
