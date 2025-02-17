#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ViewportResizeSystem final : public System
	{
	public:
		explicit ViewportResizeSystem(WorldRef world)
			: System(std::move(world))
		{
		}

		void Execute(Timestep ts) override;
	};
}
