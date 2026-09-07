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

	// Resolves a shader source path, which may belong to the ENGINE or to a GAME. Tries, in order: the
	// path as given if absolute, then the engine root, then the active project's directory. The project
	// leg is what lets a game ship its own shaders: a material in a game's project can say
	// "Shaders/Thing.frag.hlsl" and have it found next to that project rather than inside the engine.
	// Without it, every shader a game uses would have to be committed into the engine's own tree.
	//
	// Falls back to the engine root when nothing exists, so a failure reports the path a reader expects.
	std::filesystem::path ResolveShaderSource(std::string_view path);
}
