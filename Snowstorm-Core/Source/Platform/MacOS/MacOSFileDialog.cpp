#include "Snowstorm/Core/PlatformDetection.hpp"

// Dormant: PlatformDetection.hpp #errors out on macOS today (engine is Windows-only for now),
// so SS_PLATFORM_MACOS is never defined and this whole file compiles to an empty translation
// unit. Kept only to document the interface shape for when macOS support actually lands.
#ifdef SS_PLATFORM_MACOS

#include "Snowstorm/Utility/FileDialog.hpp"

namespace Snowstorm::FileDialog
{
	std::filesystem::path OpenFile(
	    const std::vector<FileDialogFilter>& filters,
	    const std::filesystem::path& defaultPath)
	{
		// TODO: implement (e.g. NSOpenPanel).
		return {};
	}

	std::filesystem::path SaveFile(
	    const std::vector<FileDialogFilter>& filters,
	    const std::filesystem::path& defaultPath)
	{
		// TODO: implement (e.g. NSSavePanel).
		return {};
	}

	std::filesystem::path OpenFolder(const std::filesystem::path& defaultPath)
	{
		// TODO: implement (e.g. NSOpenPanel with canChooseDirectories).
		return {};
	}
}

#endif // SS_PLATFORM_MACOS
