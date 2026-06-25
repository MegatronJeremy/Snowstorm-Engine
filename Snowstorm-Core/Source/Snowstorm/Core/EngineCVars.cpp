#include "pch.h"
#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};

	CVar<bool> StressScene{"scene.stress", false, "Build the procedural stress-test scene at startup instead of the saved startup world"};
}
