#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace Snowstorm::CameraControllerMath
{
	// Yaw about WORLD up, then pitch about the post-yaw right axis. Default (0, 0) looks down -Z.
	inline glm::vec3 ForwardFromPitchYaw(const float pitchRadians, const float yawRadians)
	{
		constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

		const glm::quat qYaw = glm::angleAxis(yawRadians, worldUp);
		const glm::vec3 rightAfterYaw = qYaw * glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::quat qPitch = glm::angleAxis(pitchRadians, rightAfterYaw);

		const glm::quat q = qPitch * qYaw;
		return glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
	}

	// With forward defaulting to -Z and up +Y, this yields +X for right.
	inline glm::vec3 RightFromForward(const glm::vec3& forward)
	{
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);
		return glm::normalize(glm::cross(forward, up));
	}

	// Frame-rate-independent exponential smoothing factor: fraction to move toward the
	// target this frame given a rate (1/sec) and dt. rate<=0 -> snap (no smoothing).
	inline float SmoothAlpha(const float rate, const float dt)
	{
		if (rate <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - std::exp(-rate * dt);
	}

	// Avoid exact +-90 deg to prevent singularity / weird yaw near straight up/down.
	inline float ClampLookPitch(const float pitchRadians)
	{
		constexpr float limit = glm::half_pi<float>() - 0.001f;
		return glm::clamp(pitchRadians, -limit, limit);
	}

	struct MoveKeys
	{
		bool W = false, A = false, S = false, D = false, Q = false, E = false;
	};

	// Perspective: W/S along forward, E/Q along up. Orthographic: W/S along up, E/Q along forward.
	// A/D strafe along right in both. Not normalized.
	inline glm::vec3 ComposeMoveDir(const MoveKeys& k, const glm::vec3& forward, const glm::vec3& right, const bool perspective)
	{
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);
		glm::vec3 moveDir(0.0f);

		if (k.D)
			moveDir += right;
		if (k.A)
			moveDir -= right;

		if (perspective)
		{
			if (k.W)
				moveDir += forward;
			if (k.S)
				moveDir -= forward;
			if (k.E)
				moveDir += up;
			if (k.Q)
				moveDir -= up;
		}
		else
		{
			if (k.W)
				moveDir += up;
			if (k.S)
				moveDir -= up;
			if (k.E)
				moveDir += forward;
			if (k.Q)
				moveDir -= forward;
		}
		return moveDir;
	}

	// Normalized direction times speed; zero when no key contributes (or opposing keys cancel).
	inline glm::vec3 TargetVelocity(const glm::vec3& moveDir, const float speed)
	{
		return (glm::dot(moveDir, moveDir) > 0.0f) ? glm::normalize(moveDir) * speed : glm::vec3(0.0f);
	}
}
