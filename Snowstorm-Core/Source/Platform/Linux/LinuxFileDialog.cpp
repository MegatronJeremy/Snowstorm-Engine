#include "Snowstorm/Core/PlatformDetection.hpp"

// Dormant: PlatformDetection.hpp #errors out on Linux today (engine is Windows-only for now),
// so SS_PLATFORM_LINUX is never defined and this whole file compiles to an empty translation
// unit. Kept only to document the interface shape for when Linux support actually lands.
#ifdef SS_PLATFORM_LINUX

#include "Snowstorm/Utility/FileDialog.hpp"

namespace Snowstorm::FileDialog
{
	std::filesystem::path OpenFile(const std::vector<FileDialogFilter>& filters)
	{
		// TODO: implement (e.g. GTK or the XDG desktop-portal file chooser).
		return {};
	}

	std::filesystem::path SaveFile(const std::vector<FileDialogFilter>& filters)
	{
		// TODO: implement (e.g. GTK or the XDG desktop-portal file chooser).
		return {};
	}

	std::filesystem::path OpenFolder()
	{
		// TODO: implement (e.g. GTK or the XDG desktop-portal file chooser).
		return {};
	}
}

#endif // SS_PLATFORM_LINUX
