#include "pch.h"
#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};

	CVar<bool> BakeStressScene{"scene.bake_stress", false, "Build the procedural stress-test scene, save it to Assets/Scenes/Stress.world, then exit"};

	CVar<bool> BakeSponzaScene{"scene.bake_sponza", false, "Import Assets/Meshes/Sponza/Sponza.gltf, save it to Assets/Scenes/Sponza.world, then exit"};

	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)"};
}
