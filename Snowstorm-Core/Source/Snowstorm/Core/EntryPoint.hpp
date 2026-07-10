#pragma once

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <cstdlib>
#include <string_view>

#ifdef SS_PLATFORM_WINDOWS

inline int main(int argc, char** argv)
{
	Snowstorm::Log::Init();

	// Make Vulkan validation layers discoverable regardless of how the app was launched. The vcpkg layer
	// path is baked in as a Debug-only compile definition (SS_VULKAN_LAYER_PATH, see root CMakeLists). VS
	// injects VK_ADD_LAYER_PATH via its debugger env, but Rider / a bare exe / the terminal don't — so
	// validation silently failed to load there. Set it here, before CreateApplication() creates the Vulkan
	// instance (the loader reads the var at vkCreateInstance). Only if unset, so the smoke harness / a power
	// user can still override it. Debug only (validation is a dev tool; the define is absent in Release).
#if defined(SS_DEBUG) && defined(SS_VULKAN_LAYER_PATH)
	if (std::getenv("VK_ADD_LAYER_PATH") == nullptr)
	{
		_putenv_s("VK_ADD_LAYER_PATH", SS_VULKAN_LAYER_PATH);
	}
#endif

	// Resolve console variables (env + CLI) before creating the application: startup-critical CVars
	// (e.g. Vulkan validation) are read during instance creation inside CreateApplication().
	Snowstorm::CVarRegistry::Get().Initialize(argc, argv);

	// Convenience: a bare positional argument ending in ".world" is treated as the startup scene, so
	// you can `Snowstorm-Editor.exe assets/scenes/Sponza.world` the way an editor "opens" a file —
	// no need to remember the --startup.scene= flag. An explicit --startup.scene= or SS_STARTUP_SCENE
	// (resolved above) always wins, so we only fill an otherwise-empty value.
	if (Snowstorm::CVars::StartupScene.Get().empty())
	{
		for (int i = 1; i < argc; ++i)
		{
			if (const std::string_view arg = argv[i]; !arg.starts_with("--") && arg.ends_with(".world"))
			{
				Snowstorm::CVars::StartupScene.Set(std::string(arg));
				break;
			}
		}
	}

	// Capture application startup (device/pipeline/asset init) — this happens before the frame loop, so
	// the on-demand per-frame capture can't reach it. Runtime frames are captured on demand from the
	// editor (Instrumentor::RequestCapture -> written when the frame budget elapses); shutdown is not
	// interesting enough to auto-capture.
	SS_PROFILE_BEGIN_SESSION("Startup", "SnowstormProfile-Startup.json");
	const auto app = Snowstorm::CreateApplication();
	SS_PROFILE_END_SESSION();

	app->Run();

	delete app;
}

#else
#error Snowstorm only supports Windows!
#endif
