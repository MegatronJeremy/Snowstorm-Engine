#pragma once

#include <glm/vec2.hpp>

namespace Snowstorm
{
	struct ViewportComponent
	{
		glm::vec2 Size{};
		bool Focused = true;
		bool Hovered = true;
	};
}
