#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct CameraComponent
	{
		enum class ProjectionType : uint8_t { Perspective = 0, Orthographic = 1 };

		ProjectionType Projection = ProjectionType::Perspective;

		// Perspective
		float PerspectiveFOV = 0.785398f; // radians
		float PerspectiveNear = 0.01f;
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

	void RegisterCameraComponent();
}
