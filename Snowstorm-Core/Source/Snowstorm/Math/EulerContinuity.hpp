#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cmath>

namespace Snowstorm
{
	// Euler angles (X pitch, Y yaw, Z roll; composed Ry*Rx*Rz like TransformComponent) for `rotation` that
	// stay closest to `previous`. One orientation has two Euler triples per 2*pi branch, and at pitch ±pi/2
	// yaw and roll are only constrained by their sum, so a bare extractEulerAngleYXZ jumps between
	// equivalent triples as a gizmo drag crosses those regions. Choosing the nearest triple (and, at the
	// lock, keeping the previous roll) makes consecutive frames of a continuous rotation continuous.
	inline glm::vec3 NearestEulerYXZ(const glm::mat4& rotation, const glm::vec3& previous)
	{
		constexpr float kGimbalCos = 1e-3f;
		const float pi = glm::pi<float>();
		const float twoPi = glm::two_pi<float>();

		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		glm::extractEulerAngleYXZ(rotation, yaw, pitch, roll);

		if (std::abs(std::cos(pitch)) < kGimbalCos)
		{
			// Ry(yaw) * Rx(pitch) = rotation * Rz(-roll); column 0 of Ry*Rx is (cos yaw, 0, -sin yaw).
			roll = previous.z;
			const glm::mat4 m = rotation * glm::eulerAngleZ(-roll);
			yaw = std::atan2(-m[0][2], m[0][0]);
		}

		// Shift an angle by whole turns so it lies within pi of `reference`.
		const auto unwrap = [&](const float angle, const float reference)
		{ return angle + twoPi * std::round((reference - angle) / twoPi); };

		const glm::vec3 a{unwrap(pitch, previous.x), unwrap(yaw, previous.y), unwrap(roll, previous.z)};
		const glm::vec3 b{unwrap(pi - pitch, previous.x), unwrap(yaw + pi, previous.y),
		                  unwrap(roll + pi, previous.z)};

		const glm::vec3 da = a - previous;
		const glm::vec3 db = b - previous;
		return glm::dot(db, db) < glm::dot(da, da) ? b : a;
	}
}
