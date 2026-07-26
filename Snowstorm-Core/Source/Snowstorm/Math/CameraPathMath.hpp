#pragma once

#include <glm/glm.hpp>

#include <cmath>

namespace Snowstorm
{
	// Pure math for the scripted benchmark camera orbit (#45). Data in -> data out, no ECS/registry, so it's
	// unit-testable and is the single source of truth shared by CameraPathSystem and its test. A deterministic
	// orbit (position + look-at-center yaw/pitch as a function of time) gives a REPEATABLE camera path, which
	// is what makes upscaler-vs-ground-truth metric runs comparable frame-for-frame.

	struct OrbitPose
	{
		glm::vec3 Position{0.0f};
		float Yaw = 0.0f;   // radians, about world +Y — matches TransformComponent.Rotation.y
		float Pitch = 0.0f; // radians, about camera-local right — matches TransformComponent.Rotation.x
	};

	// Camera pose at time `t` (seconds) for an orbit around `center`: circle of `radius` in the XZ plane at
	// `height` above the center, angular speed `speedRadPerSec`, always looking AT the center. Yaw/pitch are
	// derived to point the camera's forward (-Z under the engine's ForwardFromPitchYaw convention) at the
	// center, so they drop straight into TransformComponent.Rotation.{y,x}.
	inline OrbitPose OrbitPoseAt(const glm::vec3& center, const float radius, const float height,
	                             const float speedRadPerSec, const float t)
	{
		const float angle = speedRadPerSec * t;

		OrbitPose pose;
		pose.Position = center + glm::vec3(radius * std::cos(angle), height, radius * std::sin(angle));

		// Look from Position toward center. dir is the desired forward.
		const glm::vec3 dir = center - pose.Position;
		const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);

		// Yaw: the engine's forward at pitch=0 is (-sin(yaw), 0, -cos(yaw)) (ForwardFromPitchYaw, yaw about
		// world +Y). Setting that equal to the normalized horizontal direction-to-center gives
		// yaw = atan2(-dir.x, -dir.z).
		pose.Yaw = std::atan2(-dir.x, -dir.z);
		// Pitch: positive tilts up. dir.y > 0 (looking up) -> positive pitch.
		pose.Pitch = std::atan2(dir.y, horiz);
		return pose;
	}
}
