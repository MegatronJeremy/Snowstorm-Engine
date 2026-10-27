#pragma once

#include <algorithm>
#include <cstdint>

namespace Snowstorm
{
	// One loading bar from a pending count and its high-water total (PendingLoadTotal /
	// PendingCompileTotal: the most pending since the queue was last empty). Shared by every host's overlay.
	struct LoadProgress
	{
		uint32_t Done = 0;
		uint32_t Total = 0;
		float Fraction = 1.0f;

		// Nothing to show when the queue is empty or no total was recorded.
		[[nodiscard]] bool Visible() const { return Total > 0 && Done < Total; }
	};

	// A pending count above the total (a request landed after the high-water mark was read) clamps to
	// 0 done rather than wrapping.
	[[nodiscard]] constexpr LoadProgress MakeLoadProgress(const uint32_t pending, const uint32_t total)
	{
		if (total == 0)
			return {};
		const uint32_t clamped = std::min(pending, total);
		return {total - clamped, total, 1.0f - static_cast<float>(clamped) / static_cast<float>(total)};
	}
}
