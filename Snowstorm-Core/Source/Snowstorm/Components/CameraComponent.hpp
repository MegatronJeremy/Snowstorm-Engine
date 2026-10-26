#pragma once

#include <glm/vec4.hpp>

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

		// Scene3D: the lit pipeline (meshes, lights, post chain). Canvas2D: sprites only, straight into the
		// present target, with no lighting, tonemap or temporal jitter.
		enum class RenderPathType : uint8_t
		{
			Scene3D = 0,
			Canvas2D = 1
		};

		ProjectionType Projection = ProjectionType::Perspective;
		RenderPathType RenderPath = RenderPathType::Scene3D;

		// Linear colour a Canvas2D camera clears to. Scene3D backgrounds come from EnvironmentComponent.
		glm::vec4 BackgroundColor{0.0f, 0.0f, 0.0f, 1.0f};

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
	};
}
