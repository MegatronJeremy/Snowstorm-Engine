#include "EnginePaths.hpp"

#include "Snowstorm/Core/PlatformDetection.hpp"

#ifdef SS_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace Snowstorm::EnginePaths
{
	namespace
	{
		std::filesystem::path ExecutableDirectory()
		{
#ifdef SS_PLATFORM_WINDOWS
			std::wstring buffer(32768, L'\0');
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length != 0 && length < buffer.size())
			{
				buffer.resize(length);
				return std::filesystem::path(buffer).parent_path();
			}
#endif
			return std::filesystem::current_path();
		}
	}

	std::filesystem::path FindRootFrom(std::filesystem::path startDirectory)
	{
		std::error_code ec;
		startDirectory = std::filesystem::absolute(startDirectory, ec).lexically_normal();
		if (ec)
		{
			return {};
		}

		for (std::filesystem::path directory = std::move(startDirectory); !directory.empty();
		     directory = directory.parent_path())
		{
			ec.clear();
			if (std::filesystem::is_directory(directory / "Engine" / "Shaders", ec))
			{
				return directory;
			}
			if (directory == directory.root_path())
			{
				break;
			}
		}
		return {};
	}

	const std::filesystem::path& Root()
	{
		static const std::filesystem::path root = []
		{
			if (std::filesystem::path located = FindRootFrom(ExecutableDirectory()); !located.empty())
			{
				return located;
			}
			return std::filesystem::current_path();
		}();
		return root;
	}

	std::filesystem::path ShadersDirectory()
	{
		return Root() / "Engine" / "Shaders";
	}
	std::filesystem::path FontsDirectory()
	{
		return Root() / "Engine" / "Fonts";
	}
	std::filesystem::path CacheDirectory()
	{
		return Root() / "Engine" / "cache";
	}
	std::filesystem::path DefaultProjectFile()
	{
		return Root() / "Projects" / "Sandbox" / "Sandbox.ssproj";
	}
}
