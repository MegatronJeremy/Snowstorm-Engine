#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Snowstorm
{
	// A mount table mapping a virtual prefix to a real directory, so nothing outside this file has to
	// know where content physically lives. "/Game/meshes/cube.obj" is an asset's name; the directory it
	// happens to sit in today is not.
	//
	// Three mounts, and deliberately no mechanism to add more:
	//
	//   /Engine/  engine-owned content   (Engine/Shaders, Engine/Fonts) - read-only
	//   /Game/    the active project's assets                           - read-only
	//   /Cache/   generated artifacts                                   - the ONLY writable mount
	//
	// That last line is the load-bearing one, and it is Godot's res:// vs user:// split. Keeping writes
	// to a single mount is what makes a sealed or read-only content layout possible later without
	// auditing every call site that opens a file for writing.
	//
	// Resolution is LONGEST-PREFIX-WINS, not Unreal's insert-at-head shadowing. Unreal's order makes
	// resolution depend on registration order, which means a test has to reproduce that order to mean
	// anything; longest-prefix is a pure function of the table.
	//
	// Resolution never probes the filesystem. If /Game/x.png does not exist that is an error, not a
	// reason to go looking under /Engine/ - a search chain turns a typo into a silently different asset.
	namespace VirtualPath
	{
		// True if the path carries a mount prefix ("/Game/..."), false for a bare legacy path.
		[[nodiscard]] bool IsVirtual(std::string_view path);

		// Virtual -> absolute. Returns empty when the prefix names no mount, which callers must treat as
		// an error rather than falling back to a raw path.
		[[nodiscard]] std::filesystem::path Resolve(std::string_view virtualPath);

		// Absolute -> virtual, for turning a file the user picked into a storable name. Longest matching
		// mount root wins, mirroring Resolve. Empty when the file is under no mount.
		[[nodiscard]] std::optional<std::string> Virtualize(const std::filesystem::path& absolute);

		// The comparison key for "are these the same asset": forward slashes, lexically normal, and
		// lower-cased because Windows paths are case-insensitive and the same file reached by two casings
		// must not become two handles. Display keeps the original casing; only the key is folded.
		[[nodiscard]] std::string NormalizeKey(std::string_view path);

		// A multi-mesh file gives each of its parts a registry handle, distinguished by a "?submesh=N"
		// suffix on the path. That grammar existed as a hand-rolled find("?submesh=") in the asset manager
		// and a hand-rolled concatenation in the mesh library, i.e. two copies of one syntax with no owner.
		// It belongs here, with the rest of what a path can mean.
		struct AssetRef
		{
			std::string Path;
			int SubResource = -1; // -1 = the whole file
		};

		[[nodiscard]] AssetRef SplitSubResource(std::string_view path);
		[[nodiscard]] std::string JoinSubResource(std::string_view path, int subResource);

		// Where a mount points right now. /Game/ and /Cache/ follow the active project and the engine
		// root, so this is resolved per call rather than cached.
		[[nodiscard]] std::filesystem::path MountRoot(std::string_view prefix);
	}
}
