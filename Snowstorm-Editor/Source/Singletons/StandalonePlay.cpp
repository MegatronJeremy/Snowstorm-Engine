#include "StandalonePlay.hpp"

#include "Snowstorm/Core/PlatformDetection.hpp"
#include "Snowstorm/Utility/EnginePaths.hpp"

#include <fstream>

#ifdef SS_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace Snowstorm::StandalonePlay
{
	namespace fs = std::filesystem;

	std::vector<fs::path> RuntimeCandidates(const fs::path& editorDir)
	{
		const fs::path exe = "Snowstorm-Runtime.exe";
		std::vector<fs::path> out{editorDir / exe};
		if (editorDir.has_parent_path())
		{
			const fs::path targets = editorDir.parent_path().parent_path();
			out.push_back(targets / "Snowstorm-Runtime" / editorDir.filename() / exe);
		}
		return out;
	}

	std::wstring BuildCommandLine(const fs::path& runtimeExe, const fs::path& scene, const fs::path& project)
	{
		std::wstring cmd = L"\"" + runtimeExe.wstring() + L"\" \"--startup.scene=" + scene.wstring() + L"\"";
		if (!project.empty())
		{
			cmd += L" \"--startup.project=" + project.wstring() + L"\"";
		}
		return cmd;
	}

	fs::path WriteTempScene(const fs::path& cacheDir, const std::string& sceneJson)
	{
		std::error_code ec;
		fs::create_directories(cacheDir, ec);
		const fs::path path = cacheDir / "standalone-play.world";
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			return {};
		}
		out << sceneJson;
		out.flush();
		return out ? path : fs::path{};
	}

	LaunchPlan PlanLaunch(const std::filesystem::path& project, bool projectFileExists)
	{
		if (project.empty())
		{
			return {false, "Standalone play needs a project: no project is open."};
		}
		if (!projectFileExists)
		{
			return {false, "Standalone play cancelled: project file '" + project.string() +
			                   "' is missing or unreadable, the Runtime would load a different project."};
		}
		return {true, {}};
	}

	std::string Launch(const std::string& sceneJson, const fs::path& project)
	{
#ifdef SS_PLATFORM_WINDOWS
		fs::path runtimeExe;
		std::error_code ec;
		for (const fs::path& candidate : RuntimeCandidates(GetExecutableDir()))
		{
			if (fs::exists(candidate, ec))
			{
				runtimeExe = candidate;
				break;
			}
		}
		if (runtimeExe.empty())
		{
			return "Snowstorm-Runtime.exe not found next to the editor. Build the Snowstorm-Runtime target.";
		}

		const fs::path scene = WriteTempScene(GetEngineRoot() / "Engine" / "cache", sceneJson);
		if (scene.empty())
		{
			return "Could not write the temporary scene file.";
		}

		std::wstring cmd = BuildCommandLine(runtimeExe, scene, project);
		const std::wstring cwd = GetEngineRoot().wstring();
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi{};
		if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi))
		{
			return "Failed to start Snowstorm-Runtime.exe (CreateProcess error " + std::to_string(GetLastError()) + ").";
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return {};
#else
		(void)sceneJson;
		(void)project;
		return "Standalone play is only available on Windows.";
#endif
	}
}
