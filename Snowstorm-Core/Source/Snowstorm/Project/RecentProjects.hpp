#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Snowstorm
{
	struct RecentProject
	{
		std::filesystem::path Path;
		std::string Name;
		int64_t LastOpened = 0; // seconds since the Unix epoch
	};

	enum class BootAction
	{
		OpenStartupProject, // startup.project was set explicitly
		OpenRecent,         // most recent project that still exists
		ShowPicker          // nothing to open, or the picker was forced
	};

	struct BootDecision
	{
		BootAction Action = BootAction::ShowPicker;
		std::filesystem::path Path; // the project to open for the Open* actions
	};

	namespace RecentProjects
	{
		constexpr size_t kMaxEntries = 10;

		// Moves (or inserts) `entry` to the front, keyed on the lexically normal path, and caps the list.
		void Touch(std::vector<RecentProject>& list, RecentProject entry);

		// Drops entries for which `exists` is false.
		void Prune(std::vector<RecentProject>& list, const std::function<bool(const std::filesystem::path&)>& exists);

		// Malformed JSON parses to an empty list; entries without a path are skipped. Output is newest first.
		std::vector<RecentProject> Parse(const std::string& json);
		std::string Serialize(const std::vector<RecentProject>& list);

		// %APPDATA%/Snowstorm/RecentProjects.json, else $HOME/.snowstorm/RecentProjects.json; empty if neither is set.
		std::filesystem::path StorePath();
		std::vector<RecentProject> Load(const std::filesystem::path& file);
		bool Save(const std::filesystem::path& file, const std::vector<RecentProject>& list);

		// Explicit startup project wins, then the picker flag, then the newest existing recent entry.
		BootDecision DecideBoot(const std::vector<RecentProject>& recents, const std::filesystem::path& startupProject, bool startupIsExplicit,
		                        bool forcePicker, const std::function<bool(const std::filesystem::path&)>& exists);
	}
}
