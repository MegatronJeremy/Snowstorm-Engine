#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Advances the TransformComponent rotation of every entity with a RotatorComponent each frame.
	// Runs in SystemPhase::Logic. Simulation: only ticks in Play mode (RunsInEditMode == false), so
	// authored transforms stay put while editing and the gizmo doesn't fight the animation.
	class RotatorSystem final : public System
	{
	public:
		explicit RotatorSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return false; }
	};
}
