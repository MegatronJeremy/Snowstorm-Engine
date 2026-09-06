#pragma once

#include <filesystem>
#include <string_view>

namespace Snowstorm
{
	// Where the engine's OWN files live: Engine/Shaders, Engine/cache, Tools/dxc. These ship with the
	// engine and are not project content, so they resolve relative to the EXECUTABLE rather than to the
	// working directory. A game runs from its own directory with its own .ssproj, and a packaged build runs
	// from wherever it was installed; in both cases the working directory says nothing about where the
	// engine's files are. Project content resolves separately, through the active Project.
	//
	// Resolution walks up from the executable's directory looking for an "Engine/Shaders" folder: in a dev
	// build that finds the repository root, and in a shipped or staged layout it finds the folder sitting
	// beside the executable. SS_ENGINE_ROOT overrides it outright. Falls back to the working directory when
	// nothing matches, which is the behaviour everything had before this existed.
	const std::filesystem::path& EngineRoot();

	// <engine root>/Engine/cache/<kind>, the directory for a cooked-asset cache ("texture", "mesh", "ibl",
	// "shaders"). Caches are engine-owned build products keyed by content hash, so they belong beside the
	// engine rather than in whatever directory the game happened to be launched from.
	std::filesystem::path EngineCacheDir(std::string_view kind);
}
