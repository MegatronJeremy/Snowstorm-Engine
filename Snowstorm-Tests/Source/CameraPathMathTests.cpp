#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Math/CameraPathMath.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <vector>

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

// A capture names a frame and a separate reference run has to reproduce that exact viewpoint, so the
// frame -> time mapping must be closed-form. Repeated addition of 1/60 is NOT the same float as a
// multiplication once the frame index gets large, and the two runs would then be measuring viewpoints that
// drift apart the longer the path plays.
TEST_CASE("Path time is closed-form, not accumulated", "[camera][path]")
{
	CHECK(CameraPathTimeAtFrame(0) == 0.0f);
	CHECK(CameraPathTimeAtFrame(60) == Approx(1.0f));

	float accumulated = 0.0f;
	for (int i = 0; i < 900; ++i)
	{
		accumulated += kCameraPathStepSeconds;
	}
	const float closedForm = CameraPathTimeAtFrame(900);

	// Both land on 15 s to within float tolerance, so the orbit would look fine either way.
	CHECK(closedForm == Approx(15.0f));
	// They are nonetheless different bit patterns, which is exactly the drift the closed form removes.
	CHECK(accumulated != closedForm);
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

namespace
{
	// Open route with uneven spacing and a swinging aim point, so the arc-length and continuity tests are
	// exercised against a curve that is not accidentally uniform.
	std::vector<CameraWaypoint> MakeRoute()
	{
		return {
		    {{8.0f, 1.5f, -0.4f}, {-4.0f, 1.5f, -0.4f}},
		    {{4.0f, 1.5f, -0.4f}, {-6.0f, 2.4f, 0.2f}},
		    {{-1.0f, 1.6f, 0.3f}, {-9.0f, 1.2f, -1.0f}},
		    {{-7.5f, 1.5f, -0.6f}, {-11.0f, 1.5f, -0.4f}},
		};
	}
}

// The spline must pass THROUGH every waypoint (Catmull-Rom is interpolating, not approximating). If it only
// came near them, an authored route verified to clear the geometry would not be the route that is flown.
TEST_CASE("Spline interpolates its waypoints", "[camera][path][spline]")
{
	const std::vector<CameraWaypoint> wp = MakeRoute();
	for (int i = 0; i < static_cast<int>(wp.size()); ++i)
	{
		const OrbitPose p = SplinePoseAt(wp, false, static_cast<float>(i));
		CHECK(p.Position.x == Approx(wp[i].Position.x).margin(1e-4));
		CHECK(p.Position.y == Approx(wp[i].Position.y).margin(1e-4));
		CHECK(p.Position.z == Approx(wp[i].Position.z).margin(1e-4));
	}
}

// Orientation continuity is the reason waypoints carry a look-at target instead of an angle pair. Angular
// velocity must not jump at a waypoint: a motion-vector discontinuity there would be an artifact of the
// camera rig, in a route whose whole purpose is to exercise motion-vector-driven reprojection.
TEST_CASE("Spline yaw is C1 across a waypoint", "[camera][path][spline]")
{
	const std::vector<CameraWaypoint> wp = MakeRoute();
	constexpr float h = 1e-3f;

	for (int i = 1; i + 1 < static_cast<int>(wp.size()); ++i)
	{
		const float u = static_cast<float>(i);
		// One-sided angular rates either side of the waypoint.
		const float before = SplinePoseAt(wp, false, u).Yaw - SplinePoseAt(wp, false, u - h).Yaw;
		const float after = SplinePoseAt(wp, false, u + h).Yaw - SplinePoseAt(wp, false, u).Yaw;
		// Guard against passing vacuously: a route whose yaw barely moves would satisfy any continuity
		// check. MakeRoute swings the aim point precisely so this is a real measurement.
		CHECK(std::abs(before) > 1e-5f);
		CHECK(after == Approx(before).margin(2e-3));
	}
}

// Arc-length remapping is what makes world speed uniform, which is what makes the per-frame motion-vector
// magnitude comparable across the route and between runs. Equal distance steps must cover equal distance.
TEST_CASE("Spline arc-length remap gives constant world speed", "[camera][path][spline]")
{
	const std::vector<CameraWaypoint> wp = MakeRoute();
	const std::vector<glm::vec2> table = BuildSplineArcTable(wp, false, 64);
	const float total = SplineTotalLength(table);
	REQUIRE(total > 0.0f);

	constexpr int steps = 40;
	float minStep = 1e9f;
	float maxStep = 0.0f;
	glm::vec3 prev = SplinePoseAt(wp, false, SplineDistanceToU(table, 0.0f)).Position;
	for (int s = 1; s <= steps; ++s)
	{
		const float d = total * static_cast<float>(s) / static_cast<float>(steps);
		const glm::vec3 cur = SplinePoseAt(wp, false, SplineDistanceToU(table, d)).Position;
		const float len = glm::length(cur - prev);
		minStep = std::min(minStep, len);
		maxStep = std::max(maxStep, len);
		prev = cur;
	}
	// Without the remap the ratio tracks waypoint spacing; with it the steps agree to within LUT resolution.
	CHECK(maxStep / minStep == Approx(1.0f).margin(0.05));
}

// A loop must be seamless at the wrap: the pose approaching segCount and the pose at 0 are the same place,
// or a looping benchmark would jump once per lap.
TEST_CASE("Spline loop wraps seamlessly", "[camera][path][spline]")
{
	const std::vector<CameraWaypoint> wp = MakeRoute();
	const int segs = SplineSegmentCount(wp.size(), true);
	const OrbitPose atZero = SplinePoseAt(wp, true, 0.0f);
	const OrbitPose atWrap = SplinePoseAt(wp, true, static_cast<float>(segs));
	CHECK(atWrap.Position.x == Approx(atZero.Position.x).margin(1e-4));
	CHECK(atWrap.Position.y == Approx(atZero.Position.y).margin(1e-4));
	CHECK(atWrap.Position.z == Approx(atZero.Position.z).margin(1e-4));
}
