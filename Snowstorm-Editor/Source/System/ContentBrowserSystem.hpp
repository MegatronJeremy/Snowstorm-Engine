#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Assets/AssetDirectoryWatcher.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

#include <future>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Lists files under assets/ and lets the user import them into the asset registry on demand, so
	// new content becomes pickable in the inspector without hand-editing AssetRegistry.json.
	class ContentBrowserSystem final : public System
	{
	public:
		explicit ContentBrowserSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		struct Entry
		{
			std::string Path;        // generic, relative to repo root (e.g. assets/meshes/cube.obj)
			std::string DisplayName; // filename only
			AssetType Type = AssetType::None;
		};

		void Rescan();
		void PollFileChanges();

		std::vector<Entry> m_Entries;
		bool m_Scanned = false;

		// Active type filter. AssetType::None means "All". Drives the tab bar at the top of the panel.
		AssetType m_Filter = AssetType::None;
		char m_Search[128] = {};

		// File watcher (#41): the directory is stat'd on a worker every kWatchIntervalSeconds, and the
		// debouncer decides on the main thread when a change is settled enough to import.
		ChangeDebouncer m_Debouncer;
		std::future<DirectorySnapshot> m_PendingScan;
		double m_NextPoll = 0.0;
		bool m_WatchSeeded = false;
	};
}
