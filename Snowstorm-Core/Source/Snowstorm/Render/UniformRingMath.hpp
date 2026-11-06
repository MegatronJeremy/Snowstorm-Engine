#pragma once

#include <cstdint>

namespace Snowstorm
{
	enum class RingAllocStatus : uint8_t
	{
		Fits,     // placed at the aligned head
		Clamped,  // aligned head overflows; placed flush with the end of the ring, overlapping earlier allocations
		TooLarge, // larger than the whole ring; no placement exists
	};

	struct RingAllocResult final
	{
		RingAllocStatus Status = RingAllocStatus::TooLarge;
		uint32_t Offset = 0;
	};

	constexpr uint32_t RingAlignUp(const uint32_t value, const uint32_t alignment)
	{
		return (value + (alignment - 1u)) & ~(alignment - 1u);
	}

	// alignment must be a power of two. The overflow test runs in 64 bits so a head near UINT32_MAX cannot wrap into a false fit.
	constexpr RingAllocResult RingAllocate(const uint32_t head, const uint32_t capacity, const uint32_t size, const uint32_t alignment)
	{
		const uint32_t alignedHead = RingAlignUp(head, alignment);
		if (static_cast<uint64_t>(alignedHead) + size <= capacity)
		{
			return {RingAllocStatus::Fits, alignedHead};
		}
		if (size > capacity)
		{
			return {RingAllocStatus::TooLarge, 0};
		}
		return {RingAllocStatus::Clamped, capacity - size};
	}
}
