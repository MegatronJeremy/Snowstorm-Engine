#pragma once
#include "Snowstorm/ECS/System.hpp"

#include <imgui.h>

namespace Snowstorm
{
	// TODO important this runs before any other ImGui systems, and after Rendering has finished
	class DockspaceSetupSystem final : public System
	{
	public:
		explicit DockspaceSetupSystem(const WorldRef& world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Request a rebuild of the default dock layout on the next frame (View > Reset Window Layout).
		// A flag rather than a direct call because the rebuild must run inside this system's frame, right
		// before DockSpace() is submitted (the host window every panel docks into). Static so the menu
		// system can set it without a handle to this system (mirrors ConsoleSystem::s_Open).
		static void RequestResetLayout() { s_ResetRequested = true; }

	private:
		static void BuildDefaultLayout(ImGuiID dockspaceID);

		static inline bool s_ResetRequested = false;
	};
}
