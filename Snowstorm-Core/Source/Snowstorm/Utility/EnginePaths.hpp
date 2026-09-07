#pragma once

#include <filesystem>
#include <string_view>

namespace Snowstorm
{
	// Where the engine's OWN assets live: Engine/Shaders, Engine/cache, Tools/dxc. Distinct from project
	// content (Projects/<name>/assets), which resolves through Project instead.
	//
	// Resolution order, first hit wins:
	//   1. the engine.root CVar (SS_ENGINE_ROOT), used verbatim when set
	//   2. the nearest ancestor of the EXECUTABLE that contains Engine/Shaders
	//   3. the working directory
	//
	// Rule 2 rather than the working directory is what lets a moved or packaged exe find its shaders.
	// Rule 1 exists because rule 2 fails the moment the engine is CONSUMED rather than built in place: a
	// game in its own repo puts the exe under its own build tree, with Engine/ inside a submodule that is
	// not an ancestor of it. Such a game sets engine.root once and everything resolves.
	//
	// Computed once on first call and cached, so the CVar must be resolved (which EntryPoint does before
	// the Application exists) before anything asks for a path.
	const std::filesystem::path& GetEngineRoot();

	// Resolves an engine-relative path ("Engine/Shaders/Foo.hlsl") against the root above. An absolute
	// path passes through unchanged, so a caller may always route through this.
	std::filesystem::path EngineAssetPath(std::string_view relative);
}
