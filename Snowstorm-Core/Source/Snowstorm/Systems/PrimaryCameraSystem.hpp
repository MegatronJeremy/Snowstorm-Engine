#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Enforces the single-Primary-camera invariant (Unity MainCamera-tag / Unreal semantics): at most one
	// AUTHORED camera may be CameraComponent::Primary. The editor's "Set as Primary" action is already
	// exclusive, but this is the CENTRAL enforcement point that also covers the console, undo, and scene
	// deserialize (a hand-edited .world could carry two Primary cameras). Runs each frame before the camera
	// controller + render camera resolution, so downstream consumers always see a clean at-most-one state.
	// Editor Scene-view cameras (DoNotSerializeComponent) are excluded — Primary is a gameplay-only concept.
	class PrimaryCameraSystem final : public System
	{
	public:
		explicit PrimaryCameraSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
