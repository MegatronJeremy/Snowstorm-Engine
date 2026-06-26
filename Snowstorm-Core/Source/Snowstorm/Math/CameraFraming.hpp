#pragma once

#include "Bounds.hpp"
#include "Math.hpp"

namespace Snowstorm
{
	// A camera pose that frames an AABB: where to sit, where to look (pitch/yaw in the engine's
	// ForwardFromPitchYaw convention), and near/far planes fitted to the bounds' size. Pure data —
	// no ECS, no side effects — so both the bake tool and the editor "Frame" command can use it.
	struct FramingPose
	{
		glm::vec3 Position{0.0f};
		float Pitch = 0.0f; // radians, rotation about X (look up/down)
		float Yaw = 0.0f;   // radians, rotation about Y (look left/right)
		float Near = 0.1f;
		float Far = 1000.0f;
	};

	// Compute a pose that frames the whole AABB for a perspective camera with the given vertical FOV.
	// The eye is pulled back along +Z far enough that the bounding sphere fits in the frustum, lifted
	// slightly above centre, and pitched down toward the centre. Near/far are scaled to the model so
	// depth precision and clipping stay sane regardless of import scale (mm vs m vs cm models).
	inline FramingPose ComputeFramingPose(const AABB& bounds, const float fovYRadians)
	{
		const glm::vec3 center = bounds.Center();
		const glm::vec3 size = bounds.Max - bounds.Min;
		const float radius = glm::max(glm::length(size) * 0.5f, 0.001f);

		// Distance so the bounding sphere fits the vertical FOV (with a small margin).
		const float halfFov = glm::max(fovYRadians * 0.5f, 0.01f);
		const float distance = (radius / glm::sin(halfFov)) * 1.1f;

		const float lift = size.y * 0.25f;
		const glm::vec3 eye = center + glm::vec3(0.0f, lift, distance);

		FramingPose pose;
		pose.Position = eye;
		pose.Yaw = 0.0f;                         // facing -Z toward the centre
		pose.Pitch = -glm::atan(lift, distance); // look slightly down at the centre
		pose.Near = glm::max(radius * 0.001f, 0.05f);
		pose.Far = (distance + radius) * 2.0f;
		return pose;
	}
}
