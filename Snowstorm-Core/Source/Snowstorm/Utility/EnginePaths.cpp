#include "EnginePaths.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Assets/VirtualPath.hpp"
#include "Snowstorm/Project/Project.hpp"
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
			// The tree this engine was CONFIGURED from. Below the walk-up on purpose: a staged or packaged
			// build has Engine/ beside the exe and must win, or it would silently read shaders out of a
			// stale checkout on the build machine. The exists() guard makes this self-disabling when the
			// source tree is gone, degrading to the working directory rather than to a wrong path.
#ifdef SS_ENGINE_SOURCE_DIR
			if (fs::path configured(SS_ENGINE_SOURCE_DIR); fs::exists(configured / "Engine" / "Shaders", ec))
			{
				return configured;
			}
#endif

			return fs::current_path();
		}();
		return root;
	}

	fs::path ResolveShaderSource(const std::string_view path)
	{
		fs::path p(path);
		if (p.is_absolute())
		{
			return p;
		}

		// A mounted path is unambiguous: the table says exactly where it lives, and a bad prefix is an
		// error rather than a cue to look somewhere else.
		if (VirtualPath::IsVirtual(path))
		{
			return VirtualPath::Resolve(path);
		}

		// Legacy, unqualified. This is the probe the mount table exists to retire: it cannot distinguish
		// a typo from a missing file, and whichever root happens to have a matching name wins. Kept only
		// so material files written before the namespace still load.
		std::error_code ec;
		if (fs::path engineSide = GetEngineRoot() / p; fs::exists(engineSide, ec))
		{
			return engineSide;
		}

		if (const Ref<Project> project = Project::GetActive())
		{
			if (fs::path projectSide = project->GetProjectDirectory() / p; fs::exists(projectSide, ec))
			{
				return projectSide;
			}
		}

		return GetEngineRoot() / p;
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
