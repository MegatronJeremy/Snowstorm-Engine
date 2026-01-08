#pragma once

#include "Snowstorm/Render/Math.hpp"

namespace Snowstorm
{
	struct DirectionalLightComponent
	{
		glm::vec3 Direction;
		glm::vec3 Color;
		float Intensity = 1.0f;
	};

	void RegisterLightingComponents();
}

