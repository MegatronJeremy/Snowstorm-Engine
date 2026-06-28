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
}
