#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Editor "Frame" command: press F to frame the selected entity, Shift+F to frame the whole scene.
	// Moves the primary camera (position + look + near/far) to fit the target's bounds — a pure camera
	// move, no lights, no save. Shares the framing math (ComputeFramingPose) with the import bake tool.
	class CameraFocusSystem final : public System
	{
	public:
		explicit CameraFocusSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
