#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class LightingSystem : public System
	{
	public:
		explicit LightingSystem(const WorldRef& world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
