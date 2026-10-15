#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <filesystem>
#include <span>
#include <string_view>

namespace Snowstorm
{
	// Case-insensitive; accepts the extension with its dot (".PNG"). None means not importable.
	AssetType AssetTypeFromExtension(std::string_view ext);
	AssetType AssetTypeFromPath(const std::filesystem::path& path);

	// Every extension AssetTypeFromExtension maps to a type other than None, lowercase with the dot.
	std::span<const char* const> ImportableExtensions();

	// Folder under the project's asset directory an imported file of this type lands in when the caller
	// names none; the layout of the sample project. Empty for types that are not imported by copy.
	std::filesystem::path DefaultImportFolder(AssetType type);

	struct ImportPlan
	{
		// Where the asset ends up, project-relative when a project is given, else the source path itself.
		std::filesystem::path DestRel;
		std::filesystem::path DestAbs;
		// The source lies outside the project and must be copied to DestAbs.
		bool Copy = false;
		// Copy only: a different file already sits at the preferred name, so DestRel carries a _N suffix.
		bool Renamed = false;
		// Copy only: a byte-identical file already sits at DestAbs, so nothing is written and the existing
		// asset is reused (re-dropping the same file is idempotent).
		bool ReuseExisting = false;
	};

	// Pure over filesystem reads, writes nothing. projectDir empty means no active project: the absolute
	// source is referenced in place. A source under projectDir is referenced in place, project-relative.
	// Otherwise it is copied into projectDir/destFolderRel; a name collision with different content takes
	// the first free "stem_N.ext", never overwriting a file the registry may already point at.
	ImportPlan PlanImport(const std::filesystem::path& source, const std::filesystem::path& projectDir,
	                      const std::filesystem::path& destFolderRel);
} // namespace Snowstorm
