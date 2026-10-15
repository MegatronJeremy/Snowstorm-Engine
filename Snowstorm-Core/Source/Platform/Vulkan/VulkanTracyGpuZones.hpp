#pragma once

#include "Snowstorm/Render/CommandContext.hpp"

#include <vector>

namespace Snowstorm
{
	// Forwards resolved per-pass GPU scopes to Tracy as GPU zones, one Tracy context per queue. Compiles to
	// nothing unless TRACY_ENABLE is defined.
	struct TracyGpuZones
	{
		static void Emit(const std::vector<GpuScope>& scopes);
	};
}
