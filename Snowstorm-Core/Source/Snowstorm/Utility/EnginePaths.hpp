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
	//   3. SS_ENGINE_SOURCE_DIR, the tree this engine was configured from, if it still exists
	//   4. the working directory
	//
	// This is Unreal's arrangement: rule 2 is its "native project" rule (derive the engine from the
	// executable, which is why a packaged Unreal game stages engine content beside the exe), and rule 1
	// is the escape hatch its EngineAssociation registry entry provides for a project outside the tree.
	// Rule 3 sits BELOW the walk-up deliberately: a baked absolute source path must never beat a real
	// staged layout, or a relocated build reads shaders from a stale checkout.
	//
	// Rule 2 rather than the working directory is what lets a moved or packaged exe find its shaders.
	// Rule 3 exists because rule 2 fails the moment the engine is CONSUMED rather than built in place: a
	// game in its own repo puts the exe under its own build tree, with Engine/ inside a submodule that is
	// not an ancestor of it. Baking the configured tree means such a game needs no setup step at all.
	//
	// Computed once on first call and cached, so the CVar must be resolved (which EntryPoint does before
	// the Application exists) before anything asks for a path.
	const std::filesystem::path& GetEngineRoot();

	// Resolves an engine-relative path ("Engine/Shaders/Foo.hlsl") against the root above. An absolute
	// path passes through unchanged, so a caller may always route through this.
	std::filesystem::path EngineAssetPath(std::string_view relative);

	// Resolves a shader source path, which may belong to the ENGINE or to a GAME.
	//
	// A mounted path ("/Engine/Shaders/Foo.hlsl") resolves through the mount table and is unambiguous.
	// An absolute path passes through. Anything else is a legacy unqualified string and falls back to
	// probing the engine root then the active project, taking whichever exists.
	//
	// That probe is what the mount table exists to retire: it cannot tell a typo from a missing file,
	// and whichever root happens to hold a matching name wins. It survives only so material files
	// written before the namespace keep loading.
	std::filesystem::path ResolveShaderSource(std::string_view path);
}
