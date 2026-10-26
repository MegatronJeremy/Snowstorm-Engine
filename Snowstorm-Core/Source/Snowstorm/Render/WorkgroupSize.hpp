#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Square compute workgroup tile (side x side x 1) for the passes that expose it as a CVar. The shader
	// side is Engine/Shaders/Include/Workgroup.hlsli (SS_WG_X / SS_WG_Y, default 8); the C++ side must
	// dispatch with the same side, so both come from here. Pure, tested in WorkgroupSizeTests.
	inline constexpr int kDefaultWorkgroupSide = 8;

	// Sides a CVar may select. 32x32 = 1024 invocations is the Vulkan minimum limit, but leaves no headroom
	// on the register-heavy RT passes, so it is not offered. Anything else falls back to the default.
	[[nodiscard]] constexpr int ClampWorkgroupSide(const int side)
	{
		return (side == 4 || side == 8 || side == 16) ? side : kDefaultWorkgroupSide;
	}

	// Groups needed to cover `extent` invocations along one axis.
	[[nodiscard]] constexpr uint32_t WorkgroupCount(const uint32_t extent, const int side)
	{
		const auto s = static_cast<uint32_t>(ClampWorkgroupSide(side));
		return (extent + s - 1) / s;
	}

	// Appends SS_WG_X / SS_WG_Y for a non-default side. The default adds nothing, so the default permutation
	// keeps its .spv cache key and its cooked variant (and the RGA baselines stay valid).
	inline void AppendWorkgroupDefines(std::vector<std::string>& defines, const int side)
	{
		const int s = ClampWorkgroupSide(side);
		if (s == kDefaultWorkgroupSide)
		{
			return;
		}
		defines.emplace_back("SS_WG_X=" + std::to_string(s));
		defines.emplace_back("SS_WG_Y=" + std::to_string(s));
	}
}
