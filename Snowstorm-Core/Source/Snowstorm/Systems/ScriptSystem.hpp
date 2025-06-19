#pragma once

#include <Snowstorm/ECS/System.hpp>

namespace Snowstorm
{
	class ScriptSystem final : public System
	{
	public:
		explicit ScriptSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
