#pragma once

#include <filesystem>

namespace Snowstorm::EnginePaths
{
	[[nodiscard]] std::filesystem::path FindRootFrom(std::filesystem::path startDirectory);
	[[nodiscard]] const std::filesystem::path& Root();
	[[nodiscard]] std::filesystem::path ShadersDirectory();
	[[nodiscard]] std::filesystem::path FontsDirectory();
	[[nodiscard]] std::filesystem::path CacheDirectory();
}
