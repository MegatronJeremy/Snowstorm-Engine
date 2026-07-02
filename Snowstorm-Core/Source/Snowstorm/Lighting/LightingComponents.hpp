#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	struct DirectionalLightComponent
	{
		glm::vec3 Direction;
		glm::vec3 Color;
		float Intensity = 1.0f;

		// Whether this light casts shadows (the authored per-light toggle, like Unity Light.shadows /
		// Unreal "Cast Shadows"). Only the primary directional's shadows are rendered today; the global
		// render.shadows CVar is the scalability kill-switch above this.
		bool CastShadows = true;
	};

	// Positional lights. Unlike the sun, both derive their world POSITION (and, for spot, DIRECTION)
	// from the entity's TransformComponent -- the light data carries no position of its own (Unity/Unreal
	// model: a Light is a transform + parameters). This keeps a single source of truth and lets the
	// existing translate/rotate gizmo manipulate lights for free. Unshadowed for now (see #65).
	struct PointLightComponent
	{
		glm::vec3 Color{1.0f};
		float Intensity = 1.0f;
		float Range = 10.0f; // distance at which the light's contribution smoothly reaches zero
	};

	// Spot light: a point light restricted to a cone. Direction = the entity transform's forward (-Z).
	// Inner/Outer angles are the half-angles of the cone; between them the intensity falls off smoothly,
	// inside Inner it's full, past Outer it's zero.
	struct SpotLightComponent
	{
		glm::vec3 Color{1.0f};
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerAngleDeg = 20.0f; // full intensity within this half-angle
		float OuterAngleDeg = 30.0f; // zero past this half-angle (must be >= InnerAngleDeg)

		// Whether this spot casts shadows (per-light toggle, like DirectionalLightComponent::CastShadows).
		// Shadow-casting spots are assigned an atlas tile up to a cap; the global render.shadows CVar is the
		// scalability kill-switch above this.
		bool CastShadows = true;
	};
}
