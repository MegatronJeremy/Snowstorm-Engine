#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Math/CameraRoute.hpp"

#include <string>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	std::string SponzaRoutePath()
	{
		return std::string(SS_REPO_ROOT) + "/Projects/Sandbox/assets/camera-paths/sponza-bench.json";
	}

	// Interior of the Sponza nave at eye height, measured from the mesh: the floor spans x [-11.43, 10.42],
	// the end walls bound x, and the clear corridor between the colonnades is z in (-1.66, +1.08) at its
	// narrowest. These bounds are that measurement pulled in to leave margin, and they are what the committed
	// route is allowed to use. A route outside them clips the arcade or leaves the building, which is what the
	// old circular orbit did for 86.7% of its loop.
	constexpr float kMinX = -9.5f;
	constexpr float kMaxX = 9.0f;
	constexpr float kMinY = 1.0f;
	constexpr float kMaxY = 2.5f;
	constexpr float kMinZ = -1.0f;
	constexpr float kMaxZ = 0.5f;
}

// The route file has to parse, or the engine silently falls back to the orbit and a motion benchmark measures
// a completely different flight path than the one under review.
TEST_CASE("Sponza benchmark route loads", "[camera][route]")
{
	CameraRoute route;
	REQUIRE(LoadCameraRoute(SponzaRoutePath(), route));
	CHECK(route.Waypoints.size() >= 4);
	CHECK(route.Speed > 0.0f);
	CHECK_FALSE(route.Loop); // open, so it parks at the last waypoint and gives the run a static tail
	CHECK(route.Length() > 10.0f);
}

// The invariant the whole route rework exists for. Sampling the WAYPOINTS is not enough: Catmull-Rom
// overshoots outside its control points at a direction reversal, and the route has one, so the curve itself
// must be sampled densely.
TEST_CASE("Sponza benchmark route stays inside the nave", "[camera][route]")
{
	CameraRoute route;
	REQUIRE(LoadCameraRoute(SponzaRoutePath(), route));

	constexpr int kSamples = 2000;
	const float total = route.Length();
	for (int i = 0; i <= kSamples; ++i)
	{
		const float seconds = (total / route.Speed) * static_cast<float>(i) / static_cast<float>(kSamples);
		const glm::vec3 p = route.PoseAtTime(seconds).Position;

		INFO("sample " << i << " at t=" << seconds << "s -> (" << p.x << ", " << p.y << ", " << p.z << ")");
		CHECK(p.x >= kMinX);
		CHECK(p.x <= kMaxX);
		CHECK(p.y >= kMinY);
		CHECK(p.y <= kMaxY);
		CHECK(p.z >= kMinZ);
		CHECK(p.z <= kMaxZ);
	}
}

// An open route must hold its final pose once the distance passes the total length. That hold IS the static
// tail: with a constant world speed there is no other way to express "stop moving", and convergence after
// motion stops is a failure mode the denoiser literature tests separately (BMFR keeps a static-camera control;
// QRISP inserts stationary segments deliberately).
TEST_CASE("Open route parks at its end for the static tail", "[camera][route]")
{
	CameraRoute route;
	REQUIRE(LoadCameraRoute(SponzaRoutePath(), route));

	const float endTime = route.Length() / route.Speed;
	const OrbitPose atEnd = route.PoseAtTime(endTime);
	const OrbitPose wellPast = route.PoseAtTime(endTime * 2.0f);

	CHECK(wellPast.Position.x == Approx(atEnd.Position.x).margin(1e-4));
	CHECK(wellPast.Position.y == Approx(atEnd.Position.y).margin(1e-4));
	CHECK(wellPast.Position.z == Approx(atEnd.Position.z).margin(1e-4));
	CHECK(wellPast.Yaw == Approx(atEnd.Yaw).margin(1e-4));

	// And the parked pose is the last waypoint, not some clamped interior point.
	const CameraWaypoint& last = route.Waypoints.back();
	CHECK(atEnd.Position.x == Approx(last.Position.x).margin(1e-3));
	CHECK(atEnd.Position.z == Approx(last.Position.z).margin(1e-3));
}

// A missing or malformed route must leave the output empty so the caller can fall back, rather than
// half-populating it and flying a route nobody authored.
TEST_CASE("A bad route path fails cleanly", "[camera][route]")
{
	CameraRoute route;
	CHECK_FALSE(LoadCameraRoute(std::string(SS_REPO_ROOT) + "/does-not-exist.json", route));
	CHECK(route.Empty());
}
