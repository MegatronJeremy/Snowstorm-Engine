#pragma once

#ifdef SS_PLATFORM_WINDOWS

inline int main(int argc, char** argv)
{
	Snowstorm::Log::Init();

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
