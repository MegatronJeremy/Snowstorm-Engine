#pragma once

#include "Snowstorm/Math/CameraRoute.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Scripted benchmark camera orbit (#45). When camera.path is on, CameraPathSystem drives a camera along
	// a deterministic orbit (see CameraPathMath) instead of the free-fly controller, so upscaler-vs-ground-
	// truth metric runs follow the exact same motion every time and are frame-for-frame comparable. Runtime-
	// only (not serialized); the system Ensure<>s it onto controller cameras.
	struct CameraPathComponent
	{
		glm::vec3 Center{0.0f, 2.0f, 0.0f}; // point the orbit circles + looks at (world units)
		float Radius = 8.0f;                // orbit radius in the XZ plane
		float Height = 3.0f;                // camera height above Center
		float SpeedRadPerSec = 0.4f;        // angular speed (~16s per full loop at default)

		// Path-local frame index: 0 on the frame the path starts, +1 per RENDERED frame. The pose is a pure
		// function of it, so a capture can name a frame and a reference run can reproduce that exact viewpoint.
		uint64_t Frame = 0;

		// Renderer frame counter when the path started, subtracted out to give Frame. The path only starts
		// once the scene is actually resident (see CameraPathSystem): otherwise the pose at capture time would
		// depend on how long streaming happened to take, which differs run to run.
		uint64_t StartFrame = 0;
		bool Started = false;

		// Path time (seconds) the pose was evaluated at. Derived from Frame under the fixed benchmark step;
		// only the interactive (camera.path.fixed off) branch accumulates wall-clock into it.
		float Time = 0.0f;

		// Authored route from camera.path.file. Non-empty means the spline drives the camera and the orbit
		// fields above are unused. An orbit cannot express a benchmark route through an enclosed hall: it has
		// one radius and one height, so in Sponza it clips the colonnade and leaves the building.
		CameraRoute Route;
		bool RouteLoadAttempted = false;
	};
}
