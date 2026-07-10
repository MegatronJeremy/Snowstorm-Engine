#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Systems/CameraPathMath.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	// Reconstruct the camera forward from yaw/pitch the SAME way CameraControllerSystem does
	// (ForwardFromPitchYaw): yaw about world +Y, then pitch about the yawed local right. The orbit's derived
	// yaw/pitch must make this forward point at the orbit center.
	glm::vec3 ForwardFromPitchYaw(float pitch, float yaw)
	{
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		const glm::quat qYaw = glm::angleAxis(yaw, worldUp);
		const glm::vec3 rightAfterYaw = qYaw * glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::quat qPitch = glm::angleAxis(pitch, rightAfterYaw);
		return glm::normalize(qPitch * qYaw * glm::vec3(0.0f, 0.0f, -1.0f));
	}
}

// The whole point of the scripted path is a REPEATABLE benchmark: the same time must give the same pose,
// with no hidden state. (Guards the "frame-for-frame comparable metric runs" claim.)
TEST_CASE("Orbit pose is deterministic", "[camera][path]")
{
	const glm::vec3 center(1.0f, 2.0f, -3.0f);
	const OrbitPose a = OrbitPoseAt(center, 8.0f, 3.0f, 0.4f, 5.0f);
	const OrbitPose b = OrbitPoseAt(center, 8.0f, 3.0f, 0.4f, 5.0f);
	CHECK(a.Position.x == Approx(b.Position.x));
	CHECK(a.Position.y == Approx(b.Position.y));
	CHECK(a.Position.z == Approx(b.Position.z));
	CHECK(a.Yaw == Approx(b.Yaw));
	CHECK(a.Pitch == Approx(b.Pitch));
}

// Position must sit on the orbit: radius away in XZ, `height` above center.
TEST_CASE("Orbit position is on the circle", "[camera][path]")
{
	const glm::vec3 center(0.0f, 2.0f, 0.0f);
	const float radius = 8.0f, height = 3.0f;
	for (float t = 0.0f; t < 20.0f; t += 2.5f)
	{
		const OrbitPose p = OrbitPoseAt(center, radius, height, 0.4f, t);
		const float dx = p.Position.x - center.x;
		const float dz = p.Position.z - center.z;
		CHECK(std::sqrt(dx * dx + dz * dz) == Approx(radius).margin(1e-3));
		CHECK(p.Position.y == Approx(center.y + height));
	}
}

// The derived yaw/pitch must make the camera actually look AT the center — this is the invariant that keeps
// the benchmark framing on-subject. Reconstruct the forward and check it aligns with (center - position).
TEST_CASE("Orbit yaw/pitch look at the center", "[camera][path]")
{
	const glm::vec3 center(2.0f, 1.0f, -1.0f);
	for (float t = 0.0f; t < 15.0f; t += 1.7f)
	{
		const OrbitPose p = OrbitPoseAt(center, 6.0f, 2.0f, 0.5f, t);
		const glm::vec3 forward = ForwardFromPitchYaw(p.Pitch, p.Yaw);
		const glm::vec3 toCenter = glm::normalize(center - p.Position);
		// dot ~ 1 when forward aligns with the direction to the center.
		CHECK(glm::dot(forward, toCenter) == Approx(1.0f).margin(1e-4));
	}
}
