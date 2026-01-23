#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class MeshResolveSystem final : public System
	{
	public:
		explicit MeshResolveSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		void Resolve(entt::entity e) const;
	};
}
