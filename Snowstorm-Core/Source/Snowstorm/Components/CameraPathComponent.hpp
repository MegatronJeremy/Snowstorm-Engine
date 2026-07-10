#pragma once

#include <glm/glm.hpp>

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

		// Accumulated path time (seconds). Advanced each frame while the path is active; reset to 0 when the
		// path is (re)started so a benchmark always begins at the same pose. Deterministic: the pose is a pure
		// function of this time, so the same time -> the same frame.
		float Time = 0.0f;
	};
}
