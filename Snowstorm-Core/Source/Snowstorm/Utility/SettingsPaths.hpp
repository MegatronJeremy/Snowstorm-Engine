#pragma once

#include <filesystem>

namespace Snowstorm
{
	// Auto-saved user settings live inside the project they belong to, so projects never clobber each
	// other's settings and nothing lands in the repo root. `Saved/` is the gitignored per-user area.
	inline std::filesystem::path ProjectSettingsPath(const std::filesystem::path& projectDir)
	{
		return projectDir / "Saved" / "Settings.cfg";
	}
}
