#pragma once

#include "Bounds.hpp"
#include "Math.hpp"

#include <algorithm>
#include <array>

namespace Snowstorm
{
	// Plane in form: ax + by + cz + d = 0
	struct Plane
	{
		glm::vec3 Normal{0.0f, 0.0f, 1.0f};
		float D = 0.0f;

		// Signed distance from point to plane
		float Distance(const glm::vec3& p) const
		{
			return glm::dot(Normal, p) + D;
		}

		// Normalize plane so |Normal| = 1
		void Normalize()
		{
			if (const float len = glm::length(Normal); len > 0.0f)
			{
				Normal /= len;
				D /= len;
			}
		}
	};

	class Frustum
	{
	public:
		enum PlaneIndex : uint8_t
		{
			Left = 0,
			Right,
			Bottom,
			Top,
			Near,
			Far,
			Count
		};

		// Extract planes from a ViewProjection matrix
		// Assumes standard clip space
		static Frustum FromViewProjection(const glm::mat4& vp)
		{
			Frustum f;

			// GLM is column-major: vp[col][row]
			// Rows:
			const glm::vec4 row0 = {vp[0][0], vp[1][0], vp[2][0], vp[3][0]};
			const glm::vec4 row1 = {vp[0][1], vp[1][1], vp[2][1], vp[3][1]};
			const glm::vec4 row2 = {vp[0][2], vp[1][2], vp[2][2], vp[3][2]};
			const glm::vec4 row3 = {vp[0][3], vp[1][3], vp[2][3], vp[3][3]};

			// Planes: row3 +/- rowX
			f.m_Planes[Left] = PlaneFromVec4(row3 + row0);
			f.m_Planes[Right] = PlaneFromVec4(row3 - row0);
			f.m_Planes[Bottom] = PlaneFromVec4(row3 + row1);
			f.m_Planes[Top] = PlaneFromVec4(row3 - row1);
			f.m_Planes[Near] = PlaneFromVec4(row3 + row2);
			f.m_Planes[Far] = PlaneFromVec4(row3 - row2);

			for (auto& p : f.m_Planes)
			{
				p.Normalize();
			}

			return f;
		}

		const std::array<Plane, Count>& GetPlanes() const { return m_Planes; }

		// Sphere test: returns false if completely outside
		bool IntersectsSphere(const glm::vec3& center, const float radius) const
		{
			return std::ranges::all_of(m_Planes.begin(), m_Planes.end(),
			                           [&](const Plane& p)
			                           {
				                           return p.Distance(center) >= -radius;
			                           });
		}

		// AABB test using "positive vertex" method (fast)
		bool IntersectsAABB(const AABB& box) const
		{
			for (const auto& p : m_Planes)
			{
				// Select the corner most likely to be outside (in direction of plane normal)
				glm::vec3 v;
				v.x = (p.Normal.x >= 0.0f) ? box.Max.x : box.Min.x;
				v.y = (p.Normal.y >= 0.0f) ? box.Max.y : box.Min.y;
				v.z = (p.Normal.z >= 0.0f) ? box.Max.z : box.Min.z;

				if (p.Distance(v) < 0.0f)
				{
					return false;
				}
			}
			return true;
		}

	private:
		static Plane PlaneFromVec4(const glm::vec4& v)
		{
			Plane p;
			p.Normal = glm::vec3(v.x, v.y, v.z);
			p.D = v.w;
			return p;
		}

		std::array<Plane, Count> m_Planes{};
	};
}
