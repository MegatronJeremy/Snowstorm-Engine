#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Advances the TransformComponent rotation of every entity with a RotatorComponent each frame.
	// Runs in SystemPhase::Logic. (When an Edit/Play mode lands, this gets gated to Play.)
	class RotatorSystem final : public System
	{
	public:
		explicit RotatorSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
