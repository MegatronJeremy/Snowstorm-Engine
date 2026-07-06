#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Draws a centered "Loading assets…" progress bar over the viewport while the AssetManager has async
	// mesh/texture loads in flight (#84). Purely informational — assets pop in as they finish; this just
	// gives the user feedback during the initial load burst instead of a silent partially-empty scene.
	// Runs in the UI phase; no-op once everything is resident.
	class LoadingOverlaySystem final : public System
	{
	public:
		explicit LoadingOverlaySystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
