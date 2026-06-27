#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct CameraComponent
	{
		enum class ProjectionType : uint8_t
		{
			Perspective = 0,
			Orthographic = 1
		};

		ProjectionType Projection = ProjectionType::Perspective;

		// Perspective
		float PerspectiveFOV = 0.785398f; // radians
		// Depth precision in a perspective projection is dominated by the near plane (precision ∝ 1/near).
		// near=0.01 with far=500-1000 makes the far/near ratio so large that distant coplanar surfaces
		// collapse to the same depth and z-fight. 0.1 is close enough for any sane scene and ~10x the
		// usable precision.
		float PerspectiveNear = 0.1f;
		float PerspectiveFar = 500.0f;

		// Ortho
		float OrthographicSize = 10.0f;
		float OrthographicNear = -10.0f;
		float OrthographicFar = 10.0f;

		bool Primary = true;
		bool FixedAspectRatio = false;
		float AspectRatio = 16.0f / 9.0f; // used only if FixedAspectRatio

		// Optional later: exposure, tonemap, post process volume ref, etc.
	};
}
