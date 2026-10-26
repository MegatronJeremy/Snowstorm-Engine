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

		// Scroll-dolly glide velocity (units/sec along forward). Each scroll notch adds an impulse; the
		// system integrates it and decays it to zero, so zoom coasts instead of teleporting per notch.
		float ZoomVelocity = 0.0f;

		// 2D Scene view (#341): orthographic, rotation locked, drag pan, zoom about the cursor. Set by the
		// editor (scene open for a Canvas2D scene, toolbar, key); Active2D is what the system last applied, so
		// entering saves the 3D pose and leaving restores it.
		bool View2D = false;
		bool Active2D = false;
		glm::vec3 Saved3DPosition{0.0f};
		glm::vec3 Saved3DRotation{0.0f};
		bool SavedWasPerspective = true;
	};
}
