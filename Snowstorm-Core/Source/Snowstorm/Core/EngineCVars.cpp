#include "pch.h"
#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};
}
