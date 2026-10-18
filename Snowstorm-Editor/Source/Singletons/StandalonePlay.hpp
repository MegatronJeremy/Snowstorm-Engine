#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Snowstorm::StandalonePlay
{
	// Where Snowstorm-Runtime.exe may sit relative to the editor's directory, best first: beside it (a
	// staged package), then the sibling target's output directory with the same configuration name
	// (build/Snowstorm-Editor/Debug -> build/Snowstorm-Runtime/Debug).
	std::vector<std::filesystem::path> RuntimeCandidates(const std::filesystem::path& editorDir);

	// The runtime's command line: the exe, --startup.scene, and --startup.project when `project` is set.
	// Without the project the Runtime boots the default Sandbox, whose registry cannot resolve another
	// project's /Game/ assets, so the scene would load with no geometry.
	std::wstring BuildCommandLine(const std::filesystem::path& runtimeExe, const std::filesystem::path& scene,
	                              const std::filesystem::path& project = {});

	// Writes `sceneJson` to <cacheDir>/standalone-play.world and returns its path (empty on failure).
	std::filesystem::path WriteTempScene(const std::filesystem::path& cacheDir, const std::string& sceneJson);

	// What the editor does before launching. The Runtime reads the registry and the .ssproj from disk, so
	// the registry is saved first; a missing project file blocks the launch, since GameLayer would fall back
	// to an implicit project and the scene would load without its assets.
	struct LaunchPlan
	{
		bool SaveRegistry = false;
		std::string Error;
	};
	LaunchPlan PlanLaunch(const std::filesystem::path& project, bool projectFileExists);

	// Serialized scene -> temp file -> detached Snowstorm-Runtime process. Returns an empty string on
	// success, otherwise a user-facing reason.
	std::string Launch(const std::string& sceneJson, const std::filesystem::path& project = {});
}
