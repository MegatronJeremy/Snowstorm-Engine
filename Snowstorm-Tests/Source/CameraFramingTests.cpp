#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Math/CameraFraming.hpp"

#include <cmath>
#include <utility>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	constexpr float kFovY = glm::radians(60.0f);

	float BoundingRadius(const AABB& b)
	{
		return glm::length(b.Max - b.Min) * 0.5f;
	}
}

TEST_CASE("Framing pose keeps the bounding sphere inside the vertical FOV", "[math][framing]")
{
	const AABB bounds{glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f)};
	const FramingPose pose = ComputeFramingPose(bounds, kFovY);

	const float radius = BoundingRadius(bounds);
	const float distance = pose.Position.z - bounds.Center().z;

	CHECK(distance * std::sin(kFovY * 0.5f) >= radius);
}

TEST_CASE("Framing pose sits on the +Z axis through the centre, lifted and pitched down", "[math][framing]")
{
	const AABB bounds{glm::vec3(10.0f, 0.0f, -4.0f), glm::vec3(14.0f, 8.0f, 4.0f)};
	const FramingPose pose = ComputeFramingPose(bounds, kFovY);
	const glm::vec3 center = bounds.Center();

	CHECK(pose.Position.x == Approx(center.x));
	CHECK(pose.Position.y == Approx(center.y + 8.0f * 0.25f));
	CHECK(pose.Position.z > center.z);
	CHECK(pose.Yaw == 0.0f);
	CHECK(pose.Pitch < 0.0f);

	// Pitch aims the eye back at the centre: tan(-pitch) = lift / distance.
	const float distance = pose.Position.z - center.z;
	CHECK(std::tan(-pose.Pitch) == Approx(2.0f / distance));
}

TEST_CASE("Framing pose near/far scale with the model and far clears the whole sphere", "[math][framing]")
{
	const AABB small{glm::vec3(-0.5f), glm::vec3(0.5f)};
	const AABB large{glm::vec3(-500.0f), glm::vec3(500.0f)};

	const FramingPose s = ComputeFramingPose(small, kFovY);
	const FramingPose l = ComputeFramingPose(large, kFovY);

	CHECK(s.Near == Approx(0.05f)); // clamped floor for small models
	CHECK(l.Near == Approx(BoundingRadius(large) * 0.001f));
	CHECK(l.Near > s.Near);
	CHECK(l.Far > s.Far);

	for (const auto& [bounds, pose] : {std::pair{small, s}, std::pair{large, l}})
	{
		const float distance = pose.Position.z - bounds.Center().z;
		CHECK(pose.Far >= distance + BoundingRadius(bounds));
		CHECK(pose.Far > pose.Near);
	}
}

TEST_CASE("Framing pose stays finite for degenerate bounds and FOV", "[math][framing]")
{
	const AABB point{glm::vec3(3.0f), glm::vec3(3.0f)};

	for (const float fov : {0.0f, -1.0f, kFovY})
	{
		const FramingPose pose = ComputeFramingPose(point, fov);
		CHECK(std::isfinite(pose.Position.x));
		CHECK(std::isfinite(pose.Position.y));
		CHECK(std::isfinite(pose.Position.z));
		CHECK(std::isfinite(pose.Pitch));
		CHECK(pose.Near > 0.0f);
		CHECK(pose.Far > pose.Near);
		CHECK(pose.Position.z > point.Min.z);
	}
}
