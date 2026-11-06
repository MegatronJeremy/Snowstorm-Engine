#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Math/CameraPathPlayback.hpp"

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	CameraRoute TwoPointRoute(const bool loop)
	{
		CameraRoute route;
		route.Loop = loop;
		route.Speed = 1.0f;
		route.Waypoints = {
		    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f}},
		    {{4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, -10.0f}},
		    {{4.0f, 0.0f, 4.0f}, {4.0f, 0.0f, -6.0f}},
		};
		route.ArcTable = BuildSplineArcTable(route.Waypoints, route.Loop);
		return route;
	}
}

TEST_CASE("Fixed-step path holds at frame 0 until the scene is ready", "[camera][path]")
{
	CameraPathComponent path;
	AdvanceCameraPath(path, true, false, 500, -1, 0.0f);
	CHECK_FALSE(path.Started);
	CHECK(path.Frame == 0);
	CHECK(path.Time == 0.0f);

	AdvanceCameraPath(path, true, true, 500, -1, 0.0f);
	CHECK(path.Started);
	CHECK(path.StartFrame == 500);
	CHECK(path.Frame == 0);

	AdvanceCameraPath(path, true, true, 560, -1, 0.0f);
	CHECK(path.Frame == 60);
	CHECK(path.Time == Approx(1.0f));
}

TEST_CASE("Fixed-step Frame follows the renderer counter, ignoring skipped ticks and wall-clock", "[camera][path]")
{
	CameraPathComponent path;
	AdvanceCameraPath(path, true, true, 10, -1, 5.0f);
	// A skipped render frame leaves the counter where it was: the pose does not advance either.
	AdvanceCameraPath(path, true, true, 10, -1, 5.0f);
	AdvanceCameraPath(path, true, true, 10, -1, 5.0f);
	CHECK(path.Frame == 0);
	CHECK(path.Time == 0.0f);

	AdvanceCameraPath(path, true, true, 910, -1, 5.0f);
	CHECK(path.Frame == 900);
	CHECK(path.Time == CameraPathTimeAtFrame(900));
}

TEST_CASE("sim.freeze_frame clamps Frame and Time", "[camera][path]")
{
	CameraPathComponent path;
	AdvanceCameraPath(path, true, true, 0, 30, 0.0f);
	AdvanceCameraPath(path, true, true, 29, 30, 0.0f);
	CHECK(path.Frame == 29);

	AdvanceCameraPath(path, true, true, 31, 30, 0.0f);
	CHECK(path.Frame == 30);
	AdvanceCameraPath(path, true, true, 5000, 30, 0.0f);
	CHECK(path.Frame == 30);
	CHECK(path.Time == CameraPathTimeAtFrame(30));

	// A freeze of 0 is a valid frame (the pose at the path's start), not "off".
	CameraPathComponent zero;
	AdvanceCameraPath(zero, true, true, 0, 0, 0.0f);
	AdvanceCameraPath(zero, true, true, 100, 0, 0.0f);
	CHECK(zero.Frame == 0);
}

TEST_CASE("Interactive path accumulates the timestep and leaves Frame alone", "[camera][path]")
{
	CameraPathComponent path;
	AdvanceCameraPath(path, false, true, 1000, 5, 0.25f);
	AdvanceCameraPath(path, false, true, 2000, 5, 0.5f);
	CHECK(path.Time == Approx(0.75f));
	CHECK(path.Frame == 0);
	CHECK_FALSE(path.Started);
}

TEST_CASE("Open route parks at its last waypoint, a closed one wraps", "[camera][path]")
{
	const CameraRoute open = TwoPointRoute(false);
	const float endTime = open.Length() / open.Speed;

	CameraPathComponent parked;
	parked.Route = open;
	parked.Time = endTime;
	const OrbitPose atEnd = CameraPathPoseAt(parked);
	CHECK(atEnd.Position.x == Approx(4.0f).margin(1e-3));
	CHECK(atEnd.Position.z == Approx(4.0f).margin(1e-3));

	parked.Time = endTime * 3.0f;
	const OrbitPose wellPast = CameraPathPoseAt(parked);
	CHECK(wellPast.Position.x == Approx(atEnd.Position.x).margin(1e-4));
	CHECK(wellPast.Position.z == Approx(atEnd.Position.z).margin(1e-4));

	CameraPathComponent looped;
	looped.Route = TwoPointRoute(true);
	const float loopTime = looped.Route.Length() / looped.Route.Speed;
	looped.Time = 0.0f;
	const OrbitPose start = CameraPathPoseAt(looped);
	looped.Time = loopTime;
	const OrbitPose wrapped = CameraPathPoseAt(looped);
	CHECK(wrapped.Position.x == Approx(start.Position.x).margin(1e-3));
	CHECK(wrapped.Position.z == Approx(start.Position.z).margin(1e-3));

	// Half a lap past the end is half a lap in: it did not park.
	looped.Time = loopTime * 0.5f;
	const OrbitPose half = CameraPathPoseAt(looped);
	looped.Time = loopTime * 1.5f;
	const OrbitPose halfAgain = CameraPathPoseAt(looped);
	CHECK(halfAgain.Position.x == Approx(half.Position.x).margin(1e-3));
	CHECK(halfAgain.Position.z == Approx(half.Position.z).margin(1e-3));
}

TEST_CASE("Without a route the pose is the orbit, aimed at its center", "[camera][path]")
{
	CameraPathComponent path;
	path.Center = {1.0f, 2.0f, 3.0f};
	path.Radius = 5.0f;
	path.Height = 4.0f;
	path.Time = 0.0f;

	const OrbitPose pose = CameraPathPoseAt(path);
	CHECK(pose.Position.x == Approx(6.0f));
	CHECK(pose.Position.y == Approx(6.0f));
	CHECK(pose.Position.z == Approx(3.0f));
	CHECK(pose.Pitch < 0.0f); // looks down at the center
}
