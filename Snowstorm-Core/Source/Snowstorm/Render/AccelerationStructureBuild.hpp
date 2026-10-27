#pragma once

#include <cstdint>

namespace Snowstorm
{
	// Backend-free rules for acceleration-structure builds, so the Vulkan builders stay thin and the
	// decisions are testable without a device.

	enum class AsBuildPreference : uint8_t
	{
		FastTrace,
		FastBuild,
	};

	struct AsBuildOptions
	{
		AsBuildPreference preference = AsBuildPreference::FastTrace;
		bool allowCompaction = false;
	};

	// Bit values equal VkBuildAccelerationStructureFlagBitsKHR (ALLOW_COMPACTION 2, PREFER_FAST_TRACE 4,
	// PREFER_FAST_BUILD 8), pinned by AccelerationStructureBuildTests.
	constexpr uint32_t kAsFlagAllowCompaction = 0x2;
	constexpr uint32_t kAsFlagPreferFastTrace = 0x4;
	constexpr uint32_t kAsFlagPreferFastBuild = 0x8;

	[[nodiscard]] constexpr uint32_t AsBuildFlagBits(const AsBuildOptions& o)
	{
		return (o.preference == AsBuildPreference::FastTrace ? kAsFlagPreferFastTrace : kAsFlagPreferFastBuild) |
		       (o.allowCompaction ? kAsFlagAllowCompaction : 0u);
	}

	// A compacted copy is made only when the driver reports a nonzero size strictly below the built one;
	// anything else keeps the original, so a driver that reports 0 or no gain costs nothing.
	[[nodiscard]] constexpr bool ShouldCompactAs(const uint64_t builtSize, const uint64_t compactedSize)
	{
		return compactedSize > 0 && compactedSize < builtSize;
	}
}
