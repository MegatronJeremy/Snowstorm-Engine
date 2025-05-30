#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class CameraControllerSystem final : public System
	{
	public:
		explicit CameraControllerSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
