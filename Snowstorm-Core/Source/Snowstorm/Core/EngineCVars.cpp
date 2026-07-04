#include "pch.h"
#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};

	CVar<std::string> BakeScene{"scene.bake", "", "Bake a scene to Assets/Scenes/<name>.world then exit. Value: 'stress' (procedural) or a model path (.gltf/.glb/.obj/.fbx)"};

	CVar<std::string> DumpMeshTangents{"debug.dump_mesh_tangents", "", "Analyze a model's UV/tangent structure across seams (#74) then exit. Value: model path"};

	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)"};

	CVar<std::string> StartupScene{"startup.scene", "", "Path to a .world to load at startup (empty = Startup.world); e.g. assets/scenes/Sponza.world"};

	CVar<float> Exposure{"render.exposure", 1.0f, "Linear exposure multiplier applied before tonemapping (1.0 = neutral)"};

	CVar<bool> Shadows{"render.shadows", true, "Global directional shadow toggle (off = skip the shadow pass)"};

	CVar<int> ShadowResolution{"render.shadow.resolution", 2048, "Shadow-map resolution (square); changing it rebuilds the shadow target"};

	CVar<bool> ShadowSoft{"render.shadow.soft", true, "Soft shadows (3x3 PCF) when on, hard single-tap when off"};

	CVar<float> ShadowStrength{"render.shadow.strength", 1.0f, "Shadow darkness (1 = full occlusion, 0 = none)"};

	CVar<bool> IBL{"render.ibl", true, "Bake + use image-based lighting from the sky (off = analytic hemisphere ambient)"};

	CVar<float> IBLIntensity{"render.ibl.intensity", 0.75f, "Multiplier on the IBL ambient contribution"};
}
