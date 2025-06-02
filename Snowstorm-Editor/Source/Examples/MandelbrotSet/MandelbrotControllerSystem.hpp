#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class MandelbrotControllerSystem final : public System
	{
	public:
		explicit MandelbrotControllerSystem(const WorldRef world) : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
