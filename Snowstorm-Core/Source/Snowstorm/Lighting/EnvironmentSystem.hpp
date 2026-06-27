#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Reads the scene's EnvironmentComponent (if any) and pushes its values to the renderer each frame,
	// so the sky pass and the ambient term share one definition. Mirrors LightingSystem. Runs in
	// PreRender, before RenderSystem.
	class EnvironmentSystem : public System
	{
	public:
		explicit EnvironmentSystem(const WorldRef& world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
