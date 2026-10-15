#include "RecentProjects.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Snowstorm::RecentProjects
{
	using json = nlohmann::json;

	namespace
	{
		std::string Key(const std::filesystem::path& p)
		{
			return p.lexically_normal().generic_string();
		}
	}

	void Touch(std::vector<RecentProject>& list, RecentProject entry)
	{
		const std::string key = Key(entry.Path);
		std::erase_if(list, [&](const RecentProject& e)
		              { return Key(e.Path) == key; });
		list.insert(list.begin(), std::move(entry));
		if (list.size() > kMaxEntries)
			list.resize(kMaxEntries);
	}

	void Prune(std::vector<RecentProject>& list, const std::function<bool(const std::filesystem::path&)>& exists)
	{
		std::erase_if(list, [&](const RecentProject& e)
		              { return !exists(e.Path); });
	}

	std::vector<RecentProject> Parse(const std::string& text)
	{
		std::vector<RecentProject> out;
		try
		{
			const json root = json::parse(text);
			if (!root.is_object() || !root.contains("Projects") || !root["Projects"].is_array())
				return out;
			for (const json& node : root["Projects"])
			{
				if (!node.is_object() || !node.contains("Path") || !node["Path"].is_string())
					continue;
				RecentProject e;
				e.Path = std::filesystem::path(node["Path"].get<std::string>());
				if (e.Path.empty())
					continue;
				e.Name = node.value("Name", std::string{});
				e.LastOpened = node.value("LastOpened", int64_t{0});
				out.push_back(std::move(e));
			}
		}
		catch (const json::exception&)
		{
			return {};
		}
		std::stable_sort(out.begin(), out.end(), [](const RecentProject& a, const RecentProject& b)
		                 { return a.LastOpened > b.LastOpened; });
		if (out.size() > kMaxEntries)
			out.resize(kMaxEntries);
		return out;
	}

	std::string Serialize(const std::vector<RecentProject>& list)
	{
		json arr = json::array();
		for (const RecentProject& e : list)
			arr.push_back({{"Path", e.Path.generic_string()}, {"Name", e.Name}, {"LastOpened", e.LastOpened}});
		return json{{"Projects", std::move(arr)}}.dump(2);
	}

	std::filesystem::path StorePath()
	{
		if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
			return std::filesystem::path(appdata) / "Snowstorm" / "RecentProjects.json";
		if (const char* home = std::getenv("HOME"); home && *home)
			return std::filesystem::path(home) / ".snowstorm" / "RecentProjects.json";
		return {};
	}

	std::vector<RecentProject> Load(const std::filesystem::path& file)
	{
		std::ifstream in(file);
		if (!in.is_open())
			return {};
		std::stringstream ss;
		ss << in.rdbuf();
		return Parse(ss.str());
	}

	bool Save(const std::filesystem::path& file, const std::vector<RecentProject>& list)
	{
		if (file.empty())
			return false;
		std::error_code ec;
		std::filesystem::create_directories(file.parent_path(), ec);
		std::ofstream out(file);
		if (!out.is_open())
			return false;
		out << Serialize(list);
		return true;
	}

	BootDecision DecideBoot(const std::vector<RecentProject>& recents, const std::filesystem::path& startupProject, bool startupIsExplicit,
	                        bool forcePicker, const std::function<bool(const std::filesystem::path&)>& exists)
	{
		if (startupIsExplicit && exists(startupProject))
			return {BootAction::OpenStartupProject, startupProject};
		if (forcePicker)
			return {};
		for (const RecentProject& e : recents)
		{
			if (exists(e.Path))
				return {BootAction::OpenRecent, e.Path};
		}
		return {};
	}
}
