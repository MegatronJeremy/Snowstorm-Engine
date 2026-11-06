#pragma once

#include "Snowstorm/Math/Bounds.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Snowstorm::Scene2DView
{
	inline constexpr float kMinSize = 0.05f;
	inline constexpr float kMaxSize = 10000.0f;
	inline constexpr float kFrameMargin = 1.1f;

	// Cursor in view space: uv in [0,1]^2 with v down (ImGui), mapped to world offset from the view centre.
	// OrthographicSize is the full view height.
	inline glm::vec2 CursorOffset(const glm::vec2 uv, const float size, const float aspect)
	{
		return {(uv.x - 0.5f) * size * aspect, (0.5f - uv.y) * size};
	}

	// Dragging moves the content with the cursor, so the camera moves opposite. Screen y is down, world y up.
	inline glm::vec2 PanDelta(const glm::vec2 mouseDeltaPixels, const float size, const float viewportHeightPixels)
	{
		if (viewportHeightPixels <= 0.0f)
		{
			return {0.0f, 0.0f};
		}
		const float worldPerPixel = size / viewportHeightPixels;
		return {-mouseDeltaPixels.x * worldPerPixel, mouseDeltaPixels.y * worldPerPixel};
	}

	// A notch scales the size by (1 - notches * step), the factor the old ortho zoom used, but a wheel burst
	// can never invert or zero it.
	inline float ZoomedSize(const float size, const float notches, const float step)
	{
		const float factor = std::clamp(1.0f - notches * step, 0.2f, 5.0f);
		return std::clamp(size * factor, kMinSize, kMaxSize);
	}

	// New centre that keeps the world point under the cursor fixed while the size changes.
	inline glm::vec2 CenterAfterZoom(const glm::vec2 center, const float oldSize, const float newSize,
	                                 const glm::vec2 cursorUv, const float aspect)
	{
		return center + CursorOffset(cursorUv, oldSize, aspect) - CursorOffset(cursorUv, newSize, aspect);
	}

	// A sprite's quad in its own space: Size from the pivot (0,0 = bottom left), flat in z.
	inline AABB SpriteLocalAABB(const glm::vec2 size, const glm::vec2 pivot)
	{
		return {glm::vec3(-pivot * size, 0.0f), glm::vec3((glm::vec2(1.0f) - pivot) * size, 0.0f)};
	}

	struct Framing
	{
		glm::vec2 Center{0.0f};
		float Size = 1.0f;
	};

	// Smallest view containing [min, max] with a margin; the width term matters when the bounds are wider
	// than the viewport's aspect allows. Degenerate bounds frame a unit-high view around the point.
	inline Framing FrameBounds(const glm::vec2 min, const glm::vec2 max, const float aspect)
	{
		const glm::vec2 extent = max - min;
		const float a = aspect > 0.0f ? aspect : 1.0f;
		const float size = std::max(extent.y, extent.x / a) * kFrameMargin;
		return {(min + max) * 0.5f, std::clamp(size > 0.0f ? size : 1.0f, kMinSize, kMaxSize)};
	}
}
