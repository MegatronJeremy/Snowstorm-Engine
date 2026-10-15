#pragma once

#include <cstddef>
#include <cstdint>

namespace Snowstorm
{
	// Host-mapped buffers at or below this size are sub-allocated from one explicit-block-size VMA pool.
	// VMA falls back to a dedicated vkAllocateMemory per buffer when the driver prefers it; a pool with a
	// fixed block size never does, so these buffers share a few blocks instead of each owning one.
	inline constexpr size_t kSmallHostBufferMaxSize = 256 * 1024;

	// 16 blocks at the threshold; blocks are allocated on demand (minBlockCount 0).
	inline constexpr uint64_t kSmallHostBufferBlockSize = 4ull * 1024 * 1024;

	// Readback buffers need RANDOM host access (cached memory), a different memory type than the pool's
	// write-combined-friendly one, so they stay on the default allocator.
	constexpr bool UsesSmallHostBufferPool(const size_t size, const bool hostVisible, const bool readback)
	{
		return hostVisible && !readback && size > 0 && size <= kSmallHostBufferMaxSize;
	}
} // namespace Snowstorm
