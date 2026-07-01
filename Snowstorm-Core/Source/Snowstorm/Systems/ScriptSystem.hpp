#pragma once

#include <Snowstorm/ECS/System.hpp>

namespace Snowstorm
{
	// Runs entity scripts. Simulation: only ticks in Play mode (RunsInEditMode == false).
	class ScriptSystem final : public System
	{
	public:
		explicit ScriptSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return false; }
	};
}
