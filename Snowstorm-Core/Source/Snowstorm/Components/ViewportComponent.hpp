#pragma once

#include <glm/vec2.hpp>

namespace Snowstorm
{
	struct ViewportComponent
	{
		glm::vec2 Size{};
	};

	inline bool operator==(const ViewportComponent& lhs, const ViewportComponent& rhs)
	{
		return lhs.Size == rhs.Size;
	}

	void RegisterViewportComponent();
}
