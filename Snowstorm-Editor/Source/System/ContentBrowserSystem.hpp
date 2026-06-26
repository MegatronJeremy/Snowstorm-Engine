#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

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

		std::vector<Entry> m_Entries;
		bool m_Scanned = false;

		// Active type filter. AssetType::None means "All". Drives the tab bar at the top of the panel.
		AssetType m_Filter = AssetType::None;
		char m_Search[128] = {};
	};
}
