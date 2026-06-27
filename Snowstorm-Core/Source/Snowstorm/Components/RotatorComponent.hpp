#pragma once

#include <glm/vec3.hpp>

namespace Snowstorm
{
	// Spins an entity's TransformComponent continuously about a local axis. This is the minimal
	// motion source for the stress/showcase scene: a temporal upscaler needs movement to accumulate,
	// and continuously rotating occluders are what create the disocclusion the upscaler must handle.
	// Deliberately a tiny subset of a real animation system (no keyframes/curves) — the seam those
	// grow from.
	struct RotatorComponent
	{
		glm::vec3 Axis{0.0f, 1.0f, 0.0f}; // rotation axis (need not be normalized; system normalizes)
		float SpeedDegPerSec = 30.0f;     // angular speed in degrees per second
	};
}
