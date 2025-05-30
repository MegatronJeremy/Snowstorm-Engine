#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ViewportResizeSystem final : public System
	{
	public:
		explicit ViewportResizeSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
