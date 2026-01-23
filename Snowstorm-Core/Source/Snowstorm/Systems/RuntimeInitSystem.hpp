#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class RuntimeInitSystem final : public System
	{
	public:
		explicit RuntimeInitSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
