#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Moves the paddles and the ball, bounces, and scores. The entire game.
	class PongSystem final : public System
	{
	public:
		explicit PongSystem(const WorldRef world) : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Simulation, so it stays still while the scene is being authored and ticks once the editor is
		// in Play. A packaged runtime has no SimulationStateSingleton, so there it always ticks.
		[[nodiscard]] bool RunsInEditMode() const override
		{
			return false;
		}

	private:
		uint64_t m_Ticks = 0;
	};
}
