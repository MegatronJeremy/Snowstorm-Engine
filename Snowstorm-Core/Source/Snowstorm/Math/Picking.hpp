#pragma once

#include "Bounds.hpp"
#include "Math.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <limits>
#include <optional>

namespace Snowstorm
{
	struct Ray
	{
		glm::vec3 Origin{0.0f};
		glm::vec3 Direction{0.0f, 0.0f, -1.0f}; // assumed normalized
	};

	// Build a world-space ray from a viewport pixel. (px,py) is the cursor position relative to the
	// viewport's top-left; (width,height) is the viewport size in pixels. viewProjection is the
	// camera's VP (projection is RH, zero-to-one depth — matches CameraRuntimeUpdateSystem). The ray
	// starts on the near plane and points into the scene.
	inline Ray ScreenPointToRay(const float px, const float py, const float width, const float height,
	                            const glm::mat4& viewProjection)
	{
		// Pixel -> NDC. X in [-1,1] left->right; Y flipped (screen Y grows downward, NDC up). With
		// GLM_FORCE_DEPTH_ZERO_TO_ONE the near plane is z=0 in NDC.
		const float ndcX = (width > 0.0f) ? (2.0f * (px / width) - 1.0f) : 0.0f;
		const float ndcY = (height > 0.0f) ? (1.0f - 2.0f * (py / height)) : 0.0f;

		const glm::mat4 invVP = glm::inverse(viewProjection);

		glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); // near plane (z=0)
		glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);  // far plane  (z=1)

		const glm::vec3 nearW = glm::vec3(nearH) / nearH.w;
		const glm::vec3 farW = glm::vec3(farH) / farH.w;

		Ray r;
		r.Origin = nearW;
		r.Direction = glm::normalize(farW - nearW);
		return r;
	}

	// Slab-method ray/AABB intersection. Returns the entry distance t along the ray (>= 0) if the ray
	// hits the box, or std::nullopt otherwise. A ray origin inside the box returns 0.
	inline std::optional<float> RayIntersectsAABB(const Ray& ray, const AABB& box)
	{
		float tMin = 0.0f;
		float tMax = std::numeric_limits<float>::max();

		for (int axis = 0; axis < 3; ++axis)
		{
			const float origin = ray.Origin[axis];
			const float dir = ray.Direction[axis];
			const float lo = box.Min[axis];
			const float hi = box.Max[axis];

			if (glm::abs(dir) < 1e-8f)
			{
				// Ray parallel to this slab: miss if the origin is outside it.
				if (origin < lo || origin > hi)
				{
					return std::nullopt;
				}
			}
			else
			{
				const float invD = 1.0f / dir;
				float t1 = (lo - origin) * invD;
				float t2 = (hi - origin) * invD;
				if (t1 > t2)
				{
					std::swap(t1, t2);
				}
				tMin = glm::max(tMin, t1);
				tMax = glm::min(tMax, t2);
				if (tMin > tMax)
				{
					return std::nullopt;
				}
			}
		}

		return tMin;
	}
}
