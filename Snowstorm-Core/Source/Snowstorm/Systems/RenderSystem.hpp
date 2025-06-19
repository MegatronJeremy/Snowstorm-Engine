#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class RenderSystem final : public System
	{
	public:
		explicit RenderSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
