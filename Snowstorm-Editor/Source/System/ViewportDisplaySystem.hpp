#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ViewportDisplaySystem final : public System
	{
	public:
		explicit ViewportDisplaySystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
