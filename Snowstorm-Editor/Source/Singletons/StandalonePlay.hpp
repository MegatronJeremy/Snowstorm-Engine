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

	// The runtime's command line for a scene: the exe, then --startup.scene with the path quoted.
	std::wstring BuildCommandLine(const std::filesystem::path& runtimeExe, const std::filesystem::path& scene);

	// Writes `sceneJson` to <cacheDir>/standalone-play.world and returns its path (empty on failure).
	std::filesystem::path WriteTempScene(const std::filesystem::path& cacheDir, const std::string& sceneJson);

	// Serialized scene -> temp file -> detached Snowstorm-Runtime process. Returns an empty string on
	// success, otherwise a user-facing reason.
	std::string Launch(const std::string& sceneJson);
}
