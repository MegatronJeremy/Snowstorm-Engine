#include "EditorPreferences.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Snowstorm
{
	std::filesystem::path EditorPreferences::FilePath()
	{
#ifdef SS_PLATFORM_WINDOWS
		if (const char* appData = std::getenv("APPDATA"))
			return std::filesystem::path(appData) / "Snowstorm" / "EditorPreferences.json";
#endif
		if (const char* home = std::getenv("HOME"))
			return std::filesystem::path(home) / ".config" / "Snowstorm" / "EditorPreferences.json";
		return std::filesystem::current_path() / "EditorPreferences.json";
	}

	bool EditorPreferences::Load()
	{
		s_RecentProjects.clear();
		std::ifstream input(FilePath());
		if (!input)
			return true;

		try
		{
			const nlohmann::json root = nlohmann::json::parse(input);
			for (const auto& item : root.value("RecentProjects", nlohmann::json::array()))
			{
				RecentProject recent;
				recent.Name = item.value("Name", "Untitled");
				recent.Path = item.value("Path", "");
				recent.LastOpened = item.value("LastOpened", int64_t{0});
				if (!recent.Path.empty())
					s_RecentProjects.push_back(std::move(recent));
			}
			RemoveMissingProjects();
			return true;
		}
		catch (const std::exception& e)
		{
			SS_CORE_WARN("Could not read editor preferences '{}': {}", FilePath().string(), e.what());
			return false;
		}
	}

	bool EditorPreferences::Save()
	{
		const std::filesystem::path path = FilePath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
			return false;

		nlohmann::json root;
		root["RecentProjects"] = nlohmann::json::array();
		for (const RecentProject& recent : s_RecentProjects)
		{
			root["RecentProjects"].push_back({{"Name", recent.Name},
			                                  {"Path", recent.Path.generic_string()},
			                                  {"LastOpened", recent.LastOpened}});
		}

		std::ofstream output(path);
		if (!output)
			return false;
		output << root.dump(2);
		return output.good();
	}

	void EditorPreferences::RecordProject(const std::string& name, const std::filesystem::path& path)
	{
		std::error_code ec;
		const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
		const std::filesystem::path storedPath = ec ? std::filesystem::absolute(path) : normalized;
		s_RecentProjects.erase(std::remove_if(s_RecentProjects.begin(), s_RecentProjects.end(),
		                                      [&](const RecentProject& item)
		                                      { return item.Path == storedPath; }),
		                       s_RecentProjects.end());
		const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		                        std::chrono::system_clock::now().time_since_epoch())
		                        .count();
		s_RecentProjects.insert(s_RecentProjects.begin(), {name, storedPath, now});
		if (s_RecentProjects.size() > 10)
			s_RecentProjects.resize(10);
		Save();
	}

	void EditorPreferences::RemoveMissingProjects()
	{
		s_RecentProjects.erase(std::remove_if(s_RecentProjects.begin(), s_RecentProjects.end(),
		                                      [](const RecentProject& item)
		                                      { return !std::filesystem::is_regular_file(item.Path); }),
		                       s_RecentProjects.end());
		std::ranges::sort(s_RecentProjects, std::greater{}, &RecentProject::LastOpened);
	}
}
