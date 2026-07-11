// Snowstorm/Utility/FileDialog.hpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Snowstorm
{
	// One filter entry for a native file dialog, e.g. {"Snowstorm Project", "ssproj"}.
	struct FileDialogFilter
	{
		std::string Name;
		std::string Extension; // no leading dot
	};

	// Native OS file/folder pickers. Implemented per-platform (see Platform/Windows/WindowsFileDialog.cpp).
	// Every function returns an empty path if the user cancels.
	namespace FileDialog
	{
		std::filesystem::path OpenFile(const std::vector<FileDialogFilter>& filters = {}, const std::filesystem::path& defaultPath = {});
		std::filesystem::path SaveFile(const std::vector<FileDialogFilter>& filters = {});
		std::filesystem::path OpenFolder();
	}
}
