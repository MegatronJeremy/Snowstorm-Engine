#pragma once

#include <entt/entt.hpp>
#include <vector>

namespace Snowstorm
{
	// Runtime-only: per-camera list of visible renderables for THIS FRAME.
	// Owned by the camera entity.
	struct VisibilityCacheComponent
	{
		std::vector<entt::entity> VisibleMeshes;

		// Diagnostics: renderables eligible for this camera (resolved + layer-matched) before frustum
		// culling. Culled this frame == Considered - VisibleMeshes.size(). A large Considered with near-
		// zero culling is the batching-vs-culling tell: merged groups have scene-sized AABBs that never
		// fall outside the frustum.
		uint32_t Considered = 0;
	};
}
