#pragma once

#include <filesystem>
#include <vector>

namespace Snowstorm::DroppedFiles
{
	void Push(const std::filesystem::path& path);
	[[nodiscard]] std::vector<std::filesystem::path> TakeAll();
}
