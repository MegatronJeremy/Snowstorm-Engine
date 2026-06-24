#pragma once

#include <glm/vec3.hpp>

namespace Snowstorm
{
	// Runtime-only state for CameraControllerSystem.
	struct CameraControllerRuntimeComponent
	{
		bool WasRightClickHeld = false;
		bool Initialized = false;

		// Target pitch/yaw (radians) the mouse drives directly; the TransformComponent's
		// rotation is eased toward these for smooth look. Initialized from the transform.
		float TargetPitch = 0.0f;
		float TargetYaw = 0.0f;

		// Smoothed world-space move velocity (units/sec) for accel/decel.
		glm::vec3 MoveVelocity{0.0f};
	};
}
