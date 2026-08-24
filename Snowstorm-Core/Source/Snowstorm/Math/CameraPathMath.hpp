#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Fixed benchmark step. The path advances one of these per RENDERED frame, never by wall-clock, which is
	// what makes frame N map to one pose on any machine (Unreal's -deterministic / -fps pair, Unity's
	// Time.captureDeltaTime).
	inline constexpr float kCameraPathStepSeconds = 1.0f / 60.0f;

	// Path-local time for a path-local frame index. Multiplication, NOT repeated addition: a capture has to
	// answer "where was frame 900?" without replaying 900 steps, and 900 accumulated additions of 1/60 do not
	// equal 900 * (1/60) in float. The reference pass and the real-time pass must agree exactly here or they
	// are measuring different viewpoints.
	inline float CameraPathTimeAtFrame(const uint64_t frame)
	{
		return static_cast<float>(frame) * kCameraPathStepSeconds;
	}
	// Pure math for the scripted benchmark camera orbit (#45). Data in -> data out, no ECS/registry, so it's
	// unit-testable and is the single source of truth shared by CameraPathSystem and its test. A deterministic
	// orbit (position + look-at-center yaw/pitch as a function of time) gives a REPEATABLE camera path, which
	// is what makes upscaler-vs-ground-truth metric runs comparable frame-for-frame.

	struct OrbitPose
	{
		glm::vec3 Position{0.0f};
		float Yaw = 0.0f;   // radians, about world +Y — matches TransformComponent.Rotation.y
		float Pitch = 0.0f; // radians, about camera-local right — matches TransformComponent.Rotation.x
	};

	// Pose at `position` aimed at `target`. Yaw/pitch are derived to point the camera's forward (-Z under the
	// engine's ForwardFromPitchYaw convention) at the target, so they drop straight into
	// TransformComponent.Rotation.{y,x}. Roll is always 0 (level horizon).
	inline OrbitPose LookAtPose(const glm::vec3& position, const glm::vec3& target)
	{
		OrbitPose pose;
		pose.Position = position;

		const glm::vec3 dir = target - position;
		const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);

		// The engine's forward at pitch=0 is (-sin(yaw), 0, -cos(yaw)) (ForwardFromPitchYaw, yaw about world
		// +Y). Setting that equal to the normalized horizontal direction-to-target gives this yaw.
		pose.Yaw = std::atan2(-dir.x, -dir.z);
		// Positive pitch tilts up, so dir.y > 0 (looking up) gives positive pitch.
		pose.Pitch = std::atan2(dir.y, horiz);
		return pose;
	}

	// Camera pose at time `t` (seconds) for an orbit around `center`: circle of `radius` in the XZ plane at
	// `height` above the center, angular speed `speedRadPerSec`, always looking AT the center.
	inline OrbitPose OrbitPoseAt(const glm::vec3& center, const float radius, const float height,
	                             const float speedRadPerSec, const float t)
	{
		const float angle = speedRadPerSec * t;
		return LookAtPose(center + glm::vec3(radius * std::cos(angle), height, radius * std::sin(angle)), center);
	}

	// Waypoint path (camera rig rail). A Catmull-Rom spline through hand-placed interior poses, replayed
	// deterministically. This is what a benchmark route through an enclosed hall needs and an orbit cannot
	// give: the camera stays wherever the author put it (no wall clipping, no flying out of the building),
	// and the route can stage distinct motions (dolly, strafe past an occluder, reversal, a static tail)
	// rather than one uniform sweep.
	//
	// A waypoint carries a position AND a look-at target, and BOTH are splined. Orientation therefore comes
	// out C1 continuous for free, from the same curve type as position, with no quaternion path at all. The
	// obvious alternative (slerp the bracketing waypoints' orientations) is C0: angular velocity jumps at
	// every waypoint, which puts a discontinuity in the motion vectors, in a route whose entire purpose is to
	// exercise motion-vector-driven reprojection. Splining the aim point also matches how camera rigs are
	// actually authored (Unreal's camera track plus look-at track).
	struct CameraWaypoint
	{
		glm::vec3 Position{0.0f};
		glm::vec3 LookAt{0.0f};
	};

	// Uniform Catmull-Rom through p1 and p2, with p0/p3 supplying the end tangents. Written out rather than
	// pulled from glm/gtx/spline.hpp: that header is experimental, and enabling GLM_ENABLE_EXPERIMENTAL from
	// a public engine header would leak the define into every translation unit that includes this one.
	inline glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
	                            const glm::vec3& p3, const float t)
	{
		const float t2 = t * t;
		const float t3 = t2 * t;
		return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
		               (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	}

	// Control-point index for a segment: wrap for a loop, clamp for an open path (so the end segments'
	// tangents use a duplicated endpoint).
	inline int WaypointControlIndex(const int i, const int n, const bool loop)
	{
		if (loop)
		{
			const int m = i % n;
			return m < 0 ? m + n : m;
		}
		return glm::clamp(i, 0, n - 1);
	}

	// Spline segments: a loop has one per waypoint (the last wraps back to the first); an open path has n-1.
	inline int SplineSegmentCount(const size_t n, const bool loop)
	{
		if (n < 2)
		{
			return 0;
		}
		return loop ? static_cast<int>(n) : static_cast<int>(n) - 1;
	}

	// Pose at the global spline parameter `u` in [0, segCount]: segment = floor(u), local t = frac(u). `u` is
	// wrapped (loop) or clamped (open). NOT constant-speed in `u`, since equal parameter steps cover unequal
	// world distance when waypoints are unevenly spaced; feed it SplineDistanceToU for uniform world speed.
	inline OrbitPose SplinePoseAt(const std::vector<CameraWaypoint>& wp, const bool loop, float u)
	{
		const int n = static_cast<int>(wp.size());
		if (n == 0)
		{
			return {};
		}
		if (n == 1)
		{
			return LookAtPose(wp[0].Position, wp[0].LookAt);
		}

		const int segs = SplineSegmentCount(static_cast<size_t>(n), loop);
		if (loop)
		{
			u = std::fmod(u, static_cast<float>(segs));
			if (u < 0.0f)
			{
				u += static_cast<float>(segs);
			}
		}
		else
		{
			u = glm::clamp(u, 0.0f, static_cast<float>(segs));
		}

		int i = static_cast<int>(std::floor(u));
		if (i >= segs)
		{
			i = segs - 1; // u == segCount edge on an open path: stay in the last segment at t = 1
		}
		const float t = u - static_cast<float>(i);

		const int i0 = WaypointControlIndex(i - 1, n, loop);
		const int i1 = WaypointControlIndex(i, n, loop);
		const int i2 = WaypointControlIndex(i + 1, n, loop);
		const int i3 = WaypointControlIndex(i + 2, n, loop);

		return LookAtPose(
		    CatmullRom(wp[i0].Position, wp[i1].Position, wp[i2].Position, wp[i3].Position, t),
		    CatmullRom(wp[i0].LookAt, wp[i1].LookAt, wp[i2].LookAt, wp[i3].LookAt, t));
	}

	// Arc-length lookup table: (u, cumulative world length) samples across the whole spline. The camera moves
	// at a uniform world speed only if the parameter is remapped through this, and uniform speed is what makes
	// the per-frame motion-vector magnitude comparable between two runs and across the route.
	inline std::vector<glm::vec2> BuildSplineArcTable(const std::vector<CameraWaypoint>& wp, const bool loop,
	                                                  const int samplesPerSeg = 24)
	{
		std::vector<glm::vec2> table;
		table.push_back({0.0f, 0.0f});

		const int segs = SplineSegmentCount(wp.size(), loop);
		if (segs <= 0)
		{
			return table;
		}

		const int total = segs * samplesPerSeg;
		glm::vec3 prev = SplinePoseAt(wp, loop, 0.0f).Position;
		float cum = 0.0f;
		for (int s = 1; s <= total; ++s)
		{
			const float u = static_cast<float>(segs) * static_cast<float>(s) / static_cast<float>(total);
			const glm::vec3 cur = SplinePoseAt(wp, loop, u).Position;
			cum += glm::length(cur - prev);
			table.push_back({u, cum});
			prev = cur;
		}
		return table;
	}

	inline float SplineTotalLength(const std::vector<glm::vec2>& arcTable)
	{
		return arcTable.empty() ? 0.0f : arcTable.back().y;
	}

	// Pose after travelling `distance` world units along the route at uniform speed. A loop wraps; an OPEN
	// route parks at its last waypoint once distance exceeds the total length, which is what gives a route a
	// static tail (see CameraPathComponent).
	inline OrbitPose SplinePoseAtDistance(const std::vector<CameraWaypoint>& wp,
	                                      const std::vector<glm::vec2>& arcTable, const bool loop, float distance);

	// Map a constant-speed arc `distance` (world units from the start) back to the spline parameter u, by
	// binary search plus linear interpolation of the arc-length LUT. `distance` is clamped to [0, total]; the
	// caller wraps it (fmod total) for a loop before calling.
	inline float SplineDistanceToU(const std::vector<glm::vec2>& arcTable, float distance)
	{
		if (arcTable.size() < 2)
		{
			return 0.0f;
		}
		const float total = arcTable.back().y;
		if (total <= 0.0f)
		{
			return 0.0f;
		}
		distance = glm::clamp(distance, 0.0f, total);

		size_t lo = 0;
		size_t hi = arcTable.size() - 1;
		while (lo + 1 < hi)
		{
			const size_t mid = (lo + hi) / 2;
			if (arcTable[mid].y < distance)
			{
				lo = mid;
			}
			else
			{
				hi = mid;
			}
		}
		const float l0 = arcTable[lo].y;
		const float l1 = arcTable[hi].y;
		const float u0 = arcTable[lo].x;
		const float u1 = arcTable[hi].x;
		const float f = (l1 > l0) ? (distance - l0) / (l1 - l0) : 0.0f;
		return u0 + f * (u1 - u0);
	}

	inline OrbitPose SplinePoseAtDistance(const std::vector<CameraWaypoint>& wp,
	                                      const std::vector<glm::vec2>& arcTable, const bool loop, float distance)
	{
		if (const float total = SplineTotalLength(arcTable); loop && total > 0.0f)
		{
			distance = std::fmod(distance, total);
			if (distance < 0.0f)
			{
				distance += total;
			}
		}
		return SplinePoseAt(wp, loop, SplineDistanceToU(arcTable, distance));
	}
}
