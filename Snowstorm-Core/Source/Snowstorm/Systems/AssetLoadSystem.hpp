#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Main-thread pump for async asset loads (#84). Runs in the AssetSync phase, BEFORE the Resolve phase,
	// so any mesh a worker finished this frame is GPU-finalized and cache-resident by the time
	// MeshResolveSystem asks for it. This is where the deferred GPU uploads (Vulkan buffer creation) for
	// worker-cooked meshes actually happen — it must run on the main/render thread.
	class AssetLoadSystem final : public System
	{
	public:
		explicit AssetLoadSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
