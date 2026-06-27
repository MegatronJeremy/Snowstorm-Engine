#include "pch.h"
#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};

	CVar<std::string> BakeScene{"scene.bake", "", "Bake a scene to Assets/Scenes/<name>.world then exit. Value: 'stress' (procedural) or a model path (.gltf/.glb/.obj/.fbx)"};

	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)"};

	CVar<std::string> StartupScene{"startup.scene", "", "Path to a .world to load at startup (empty = Startup.world); e.g. assets/scenes/Sponza.world"};
}
