#pragma once

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Snowstorm
{
	// Pure per-entity rotation advance: rotate `tr` about `rot.Axis` by `rot.SpeedDegPerSec * dt`. Data
	// in -> data out, no ECS/registry/threading — so it's the single source of truth shared by
	// RotatorSystem (the real loop) and the parallel-ECS benchmark (which times it in isolation), and it
	// is directly unit-testable. Keeping it a free function is the "pure testable core" the engine's
	// debuggability guidance calls for. Returns without touching `tr` when there's no axis/speed.
	inline void AdvanceRotation(TransformComponent& tr, const RotatorComponent& rot, const float dt)
	{
		const float lenSq = glm::dot(rot.Axis, rot.Axis);
		if (lenSq < 1e-12f || rot.SpeedDegPerSec == 0.0f)
		{
			return; // no axis or no speed -> nothing to do
		}

		const glm::vec3 axis = rot.Axis / glm::sqrt(lenSq);
		const float deltaAngle = glm::radians(rot.SpeedDegPerSec) * dt;

		// Build the current orientation with the SAME Y->X->Z order TransformComponent uses (Ry * Rx * Rz),
		// apply the incremental rotation about the (local) axis, then read the Euler angles back in that
		// order so TransformComponent.Rotation stays canonical and the round-trip introduces no drift.
		const glm::mat4 currentMat = glm::eulerAngleYXZ(tr.Rotation.y, tr.Rotation.x, tr.Rotation.z);
		const glm::quat current = glm::quat_cast(currentMat);
		const glm::quat delta = glm::angleAxis(deltaAngle, axis);
		const glm::quat next = glm::normalize(current * delta);

		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		glm::extractEulerAngleYXZ(glm::mat4_cast(next), yaw, pitch, roll);

		tr.Rotation = glm::vec3(pitch, yaw, roll);
	}
}
