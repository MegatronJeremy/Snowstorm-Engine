#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

// CPU mirror of EnsureValidReflection in Engine/Shaders/PathTrace.comp.hlsl (#155): Blender Cycles'
// ensure_valid_reflection (intern/cycles/kernel/closure/bsdf_util.h), after Schussler et al. 2017.
namespace Snowstorm::ValidReflection
{
	// Smallest rotation of N towards Ng that lifts R = 2 (N.I) N - I to at least min(0.9 Ng.I, 0.01) above the
	// geometric surface. I points away from the surface (towards the viewer); Ng and N are unit, Ng.I > 0.
	// With N' = x X + z Ng (X = N projected onto the Ng plane, normalised), N'.R' = threshold squares into
	// 4A z^4 - 4 (A + Iz t) z^2 + (Iz + t)^2 = 0, A = Ix^2 + Iz^2, and the two roots in z^2 are
	// ((A + Iz t) +- sqrt(Ix^2 (A - t^2))) / 2A. Squaring admits a root with the wrong sign of Ix, so a root is
	// kept only if the reflection it yields clears the threshold. Returns Ng when no root is valid.
	inline glm::vec3 Apply(const glm::vec3& Ng, const glm::vec3& I, const glm::vec3& N)
	{
		const glm::vec3 R = 2.0f * glm::dot(N, I) * N - I;
		const float threshold = std::min(0.9f * glm::dot(Ng, I), 0.01f);
		if (glm::dot(Ng, R) >= threshold)
		{
			return N;
		}

		const float NdotNg = glm::dot(N, Ng);
		const glm::vec3 xRaw = N - NdotNg * Ng;
		if (glm::dot(xRaw, xRaw) < 1e-12f)
		{
			return Ng;
		}
		const glm::vec3 X = glm::normalize(xRaw);

		const float Ix = glm::dot(I, X);
		const float Iz = glm::dot(I, Ng);
		const float A = Ix * Ix + Iz * Iz;
		const float b = std::sqrt(std::max(Ix * Ix * (A - threshold * threshold), 0.0f));
		const float c = Iz * threshold + A;
		const float fac = 0.5f / A;
		const float z2[2] = {fac * (c + b), fac * (c - b)};

		const float origX = std::sqrt(std::max(1.0f - NdotNg * NdotNg, 0.0f));
		float bestDot = -2.0f;
		glm::vec3 best = Ng;
		for (const float z : z2)
		{
			if (!(z > 1e-5f && z <= 1.0f + 1e-5f))
			{
				continue;
			}
			const float nz = std::sqrt(std::min(z, 1.0f));
			const float nx = std::sqrt(std::max(1.0f - z, 0.0f));
			const glm::vec3 Nn = nx * X + nz * Ng;
			const glm::vec3 Rn = 2.0f * glm::dot(Nn, I) * Nn - I;
			if (glm::dot(Ng, Rn) < threshold - 1e-4f)
			{
				continue;
			}
			const float closeness = nx * origX + nz * NdotNg;
			if (closeness > bestDot)
			{
				bestDot = closeness;
				best = Nn;
			}
		}
		return best;
	}
}
