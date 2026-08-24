#pragma once

#include "Snowstorm/Math/CameraPathMath.hpp"

#include <string>
#include <vector>

namespace Snowstorm
{
	// An authored camera route: the waypoints, the arc-length table built from them, and how fast to fly it.
	// Loaded from the JSON camera.path.file names; see LoadCameraRoute for the format.
	struct CameraRoute
	{
		std::vector<CameraWaypoint> Waypoints;
		std::vector<glm::vec2> ArcTable; // (u, cumulative length), built with Waypoints
		float Speed = 2.0f;              // world units/sec along the arc
		bool Loop = false;

		[[nodiscard]] bool Empty() const { return Waypoints.empty(); }
		[[nodiscard]] float Length() const { return SplineTotalLength(ArcTable); }

		// Pose after `seconds` of flight. An OPEN route parks at its last waypoint once the distance passes
		// the total length, which is what gives the route its static tail.
		[[nodiscard]] OrbitPose PoseAtTime(const float seconds) const
		{
			return SplinePoseAtDistance(Waypoints, ArcTable, Loop, Speed * seconds);
		}
	};

	// Parse a route file and build its arc table. Format:
	//   { "Version": 1, "Speed": 2.0, "Loop": false,
	//     "Waypoints": [ { "Position": [x,y,z], "LookAt": [x,y,z] }, ... ] }
	// Returns false and logs on any problem, leaving `out` empty, so a caller falls back rather than flying a
	// half-parsed route: a route that silently differs from the authored one invalidates whatever it measures.
	bool LoadCameraRoute(const std::string& path, CameraRoute& out);
}
