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
	};
}
