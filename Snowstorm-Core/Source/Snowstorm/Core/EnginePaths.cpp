#include "EnginePaths.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Snowstorm
{
	namespace
	{
		namespace fs = std::filesystem;

		fs::path ExecutableDir()
		{
#ifdef _WIN32
			wchar_t buf[MAX_PATH]{};
			const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
			if (n == 0 || n == MAX_PATH)
			{
				return fs::current_path(); // fall back to the CWD if the query fails or truncates
			}
			return fs::path(buf).parent_path();
#else
			return fs::current_path();
#endif
		}

		fs::path ResolveEngineRoot()
		{
			std::error_code ec;
			if (const char* override = std::getenv("SS_ENGINE_ROOT"); override && *override)
			{
				return fs::path(override);
			}
			for (fs::path dir = ExecutableDir(); !dir.empty(); dir = dir.parent_path())
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
		}
	}

	const fs::path& EngineRoot()
	{
		static const fs::path root = ResolveEngineRoot();
		return root;
	}

	fs::path EngineCacheDir(const std::string_view kind)
	{
		return EngineRoot() / "Engine" / "cache" / kind;
	}
}
