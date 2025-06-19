#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ShaderReloadSystem final : public System
	{
	public:
		explicit ShaderReloadSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
