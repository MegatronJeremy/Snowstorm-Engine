#pragma once

#include "Snowstorm/ECS/System.hpp"

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
	};
}
