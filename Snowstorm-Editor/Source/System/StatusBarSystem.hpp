#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Draws the bottom status bar (ambient last-action text + build config). MUST run before
	// DockspaceSetupSystem: BeginViewportSideBar reserves work-area from the main viewport, and the
	// dockspace host window sizes itself to viewport->WorkSize — so the bar has to claim its strip
	// first or the dockspace would overlap it.
	class StatusBarSystem final : public System
	{
	public:
		using System::System;
		void Execute(Timestep ts) override;
	};
}
