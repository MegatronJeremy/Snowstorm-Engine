#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace Snowstorm
{
	// Maps a TLAS instance index back to the entity that produced it (RT editor picking). TlasBuildSystem
	// gathers one instance per (Transform + resolved-Mesh) entity in view order and stamps
	// instanceCustomIndex = i (VulkanTlas::Build); the GPU pick trace returns that same i via
	// CommittedInstanceID(), and the editor resolves Instances[i] -> entity. Rebuilt in lockstep with the
	// TLAS (same gather loop), so it never drifts from what the GPU traces. A pick that lands while the
	// table is momentarily stale (mid-edit) just mis-resolves one click and self-corrects on the next —
	// acceptable, no locking. Empty until the first RT-enabled TLAS build.
	class TlasInstanceMapSingleton final : public Singleton
	{
	public:
		std::vector<entt::entity> Instances;
	};
}
