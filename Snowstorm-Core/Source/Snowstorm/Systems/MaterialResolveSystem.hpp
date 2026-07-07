#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <entt/entt.hpp>

#include <unordered_set>

namespace Snowstorm
{
	class MaterialResolveSystem final : public System
	{
	public:
		explicit MaterialResolveSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Entities whose material couldn't resolve this frame because its pipeline wasn't ready yet
		// (shaders still compiling asynchronously). Retried every frame until resolution succeeds, then
		// dropped — this is what makes geometry stream in as shaders finish, instead of a Changed-once
		// entity staying invisible forever.
		std::unordered_set<entt::entity> m_PendingResolve;
	};
}
