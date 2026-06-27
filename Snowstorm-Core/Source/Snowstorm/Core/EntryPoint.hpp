#pragma once

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <string_view>

#ifdef SS_PLATFORM_WINDOWS

inline int main(int argc, char** argv)
{
	Snowstorm::Log::Init();

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

	SS_PROFILE_BEGIN_SESSION("Startup", "SnowstormProfile-Startup.json");
	const auto app = Snowstorm::CreateApplication();
	SS_PROFILE_END_SESSION();

	SS_PROFILE_BEGIN_SESSION("Runtime", "SnowstormProfile-Runtime.json");
	app->Run();
	SS_PROFILE_END_SESSION();

	SS_PROFILE_BEGIN_SESSION("Shutdown", "SnowstormProfile-Shutdown.json");
	delete app;
	SS_PROFILE_END_SESSION();
}

#else
#error Snowstorm only supports Windows!
#endif
