#pragma once

#include <glm/vec2.hpp>

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	struct MandelbrotControllerComponent
	{
		AssetHandle Material{};
		glm::vec2 Center{-1.25066f, 0.02012f};
		float Zoom{4.0f};
		int MaxIterations{1000};

		MandelbrotControllerComponent() = default;
	};
}
