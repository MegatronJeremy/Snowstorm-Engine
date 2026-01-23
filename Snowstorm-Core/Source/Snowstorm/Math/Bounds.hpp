#pragma once

#include "Math.hpp"

namespace Snowstorm
{
	struct AABB
	{
		glm::vec3 Min{0.0f};
		glm::vec3 Max{0.0f};

		glm::vec3 Center() const { return (Min + Max) * 0.5f; }
		glm::vec3 Extents() const { return (Max - Min) * 0.5f; }
	};

	struct Sphere
	{
		glm::vec3 Center{0.0f};
		float Radius = 0.0f;
	};

	struct MeshBounds
	{
		AABB Box;
		Sphere Sphere;
	};

	// Transform a local-space AABB by a full TRS matrix into a world-space AABB.
	// This uses the "center + extents" method:
	//   - Transform center by M
	//   - Transform extents by |linear(M)| (abs of 3x3)
	// This is fast and correct for any rotation + non-uniform scale (gives tight AABB in world axes).
	inline AABB TransformAABB(const AABB& local, const glm::mat4& M)
	{
		const glm::vec3 c = local.Center();
		const glm::vec3 e = local.Extents();

		const glm::vec3 worldCenter = glm::vec3(M * glm::vec4(c, 1.0f));

		// Linear part (upper-left 3x3)
		const glm::mat3 L = glm::mat3(M);

		// abs(L) to conservatively transform extents
		glm::mat3 A;
		A[0] = glm::abs(L[0]);
		A[1] = glm::abs(L[1]);
		A[2] = glm::abs(L[2]);

		const glm::vec3 worldExtents = A * e;

		AABB out;
		out.Min = worldCenter - worldExtents;
		out.Max = worldCenter + worldExtents;
		return out;
	}

	// Transform a local-space sphere by TRS matrix.
	// Radius scales by the maximum axis scale (conservative for non-uniform scale).
	inline Sphere TransformSphere(const Sphere& local, const glm::mat4& M)
	{
		Sphere out;
		out.Center = glm::vec3(M * glm::vec4(local.Center, 1.0f));

		const glm::mat3 L = glm::mat3(M);

		// Basis vector lengths represent scale after rotation
		const float sx = glm::length(L[0]);
		const float sy = glm::length(L[1]);
		const float sz = glm::length(L[2]);
		const float smax = glm::max(sx, glm::max(sy, sz));

		out.Radius = local.Radius * smax;
		return out;
	}
}
