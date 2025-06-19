#pragma once

#include <glm/glm.hpp>

namespace Snowstorm
{
	constexpr int MAX_DIRECTIONAL_LIGHTS = 4;

	struct GPUDirectionalLight
	{
		glm::vec3 Direction;
		float Intensity;
		glm::vec3 Color;
		float Padding = 0.0f;
	};

	struct LightDataBlock
	{
		GPUDirectionalLight Lights[MAX_DIRECTIONAL_LIGHTS];
		int LightCount = 0;
		float Padding[3] = { 0, 0, 0 };
	};
}
