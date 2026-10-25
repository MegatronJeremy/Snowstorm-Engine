#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Snowstorm::AssetLoadRule
{
	// Each finalize is a staging copy plus a queue wait, so a whole batch in one frame is a ~1 s stall.
	inline constexpr size_t kMaxMeshFinalizePerFrame = 6;
	inline constexpr size_t kMaxTextureFinalizePerFrame = 4;

	// Entries finalized this frame; the rest of the batch is re-queued and stays in flight.
	[[nodiscard]] constexpr size_t FinalizeCount(const size_t batchSize, const size_t budget)
	{
		return std::min(batchSize, budget);
	}

	// A failed entry is still drained (it leaves the in-flight set) but never reaches the GPU.
	[[nodiscard]] constexpr bool ShouldFinalize(const bool cpuLoadSucceeded)
	{
		return cpuLoadSucceeded;
	}

	// Entries re-queued past the budget remain in flight, so they count until actually finalized.
	[[nodiscard]] constexpr uint32_t PendingCount(const size_t inFlightMeshes, const size_t inFlightTextures)
	{
		return static_cast<uint32_t>(inFlightMeshes + inFlightTextures);
	}

	// The progress high-water mark restarts only once nothing of either kind is in flight.
	[[nodiscard]] constexpr bool ShouldResetPendingTotal(const size_t inFlightMeshes, const size_t inFlightTextures)
	{
		return inFlightMeshes == 0 && inFlightTextures == 0;
	}

	// A key sitting in a completed queue decoded fine and waits for the main-thread upload; any other is still on a worker.
	[[nodiscard]] constexpr const char* StageName(const bool inCompletedQueue)
	{
		return inCompletedQueue ? "decoded, awaiting GPU upload" : "on a worker";
	}

	// Slot 0 is untextured; every other slot is real unless it is still a placeholder.
	[[nodiscard]] constexpr bool IsSlotResident(const uint32_t slot, const bool stillPlaceholder)
	{
		return slot == 0 || !stillPlaceholder;
	}
}
