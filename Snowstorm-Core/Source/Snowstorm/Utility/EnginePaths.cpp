#include "EnginePaths.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/PlatformDetection.hpp"

#ifdef SS_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace Snowstorm
{
	namespace
	{
		namespace fs = std::filesystem;

		// Directory of the running executable (.../build/<target>/<config>/Foo.exe -> that folder).
		fs::path GetExeDir()
		{
#ifdef SS_PLATFORM_WINDOWS
			wchar_t buf[MAX_PATH]{};
			const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
			if (n == 0 || n == MAX_PATH)
			{
				return fs::current_path(); // fall back to CWD if the query fails or truncates
			}
			return fs::path(buf).parent_path();
#else
			return fs::current_path();
#endif
		}
	}

	const fs::path& GetEngineRoot()
	{
		static const fs::path root = []
		{
			if (const std::string& configured = CVars::EngineRoot.Get(); !configured.empty())
			{
				return fs::path(configured);
			}

			std::error_code ec;
			for (fs::path dir = GetExeDir(); !dir.empty(); dir = dir.parent_path())
			{
				if (fs::exists(dir / "Engine" / "Shaders", ec))
				{
					return dir;
				}
				if (dir == dir.root_path())
				{
					break;
				}
			}
			return fs::current_path();
		}();
		return root;
	}

	fs::path EngineAssetPath(const std::string_view relative)
	{
		fs::path p(relative);
		if (p.is_absolute())
		{
			return p;
		}
		return GetEngineRoot() / p;
	}
}
