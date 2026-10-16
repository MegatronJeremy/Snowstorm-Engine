#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <span>

namespace Snowstorm
{
	// One alpha-blended instance awaiting the back-to-front flush. Sequence is the submission index; it breaks
	// depth ties so the order is identical across runs (the perf and quality gates need bit-stable output).
	struct TransparentDraw
	{
		glm::vec3 BoundsCenter{0.0f}; // world space
		uint32_t Sequence = 0;
		float ViewDepth = 0.0f; // filled by SortTransparentDraws
	};

	// Sorts far-first by view-axis distance of the world-space bounds centre. The bounds centre rather than the
	// transform origin, because imported meshes often have their origin at the base. Depth is signed, so draws
	// behind the camera sort last and are left to the frustum cull.
	inline void SortTransparentDraws(const std::span<TransparentDraw> draws, const glm::vec3& cameraPosition,
	                                 const glm::vec3& cameraForward)
	{
		for (TransparentDraw& d : draws)
			d.ViewDepth = glm::dot(d.BoundsCenter - cameraPosition, cameraForward);

		std::sort(draws.begin(), draws.end(), [](const TransparentDraw& a, const TransparentDraw& b)
		          {
			if (a.ViewDepth != b.ViewDepth)
				return a.ViewDepth > b.ViewDepth;
			return a.Sequence < b.Sequence; });
	}
}
