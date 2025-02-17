#pragma once
#include "Snowstorm/ECS/System.hpp"

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
	};
}
