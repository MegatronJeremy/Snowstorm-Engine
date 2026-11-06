#pragma once

#include <glm/vec2.hpp>

namespace Snowstorm
{
	// TODO Runtime-only editor component - SHOULD ONLY BE SERIALIZED IN DEBUG BUILDS
	struct ViewportInteractionComponent
	{
		bool Focused = false;
		bool Hovered = false;
		// Cursor inside the viewport image, 0..1 with v down. Meaningful while Hovered.
		glm::vec2 CursorUv{0.5f, 0.5f};
	};

	inline bool operator==(const ViewportInteractionComponent& lhs, const ViewportInteractionComponent& rhs)
	{
		return lhs.Focused == rhs.Focused && lhs.Hovered == rhs.Hovered && lhs.CursorUv == rhs.CursorUv;
	}
};