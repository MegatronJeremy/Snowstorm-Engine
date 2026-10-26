#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Snowstorm::EmissiveLightMath
{
	struct TriangleSample
	{
		glm::vec3 Position{0.0f};
		glm::vec3 Barycentric{0.0f};
	};

	inline glm::vec3 TriangleCross(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2)
	{
		return glm::cross(p1 - p0, p2 - p0);
	}

	inline float TriangleArea(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2)
	{
		return 0.5f * glm::length(TriangleCross(p0, p1, p2));
	}

	// PBRT-v4 SampleUniformTriangle: area-preserving, so a stratified (u0, u1) set stays stratified on the triangle.
	// Density over area is 1 / TriangleArea.
	inline TriangleSample SampleTriangleUniform(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
	                                            float u0, float u1)
	{
		float b0;
		float b1;
		if (u0 < u1)
		{
			b0 = u0 * 0.5f;
			b1 = u1 - b0;
		}
		else
		{
			b1 = u1 * 0.5f;
			b0 = u0 - b1;
		}
		const float b2 = 1.0f - b0 - b1;
		TriangleSample s;
		s.Barycentric = glm::vec3(b0, b1, b2);
		s.Position = b0 * p0 + b1 * p1 + b2 * p2;
		return s;
	}

	// Converts an area-measure density at a point on a light to a solid-angle density at the shading point:
	// pdf_w = pdf_a * d^2 / |cos(theta_light)|. Returns 0 at grazing or back-facing angles, where the sample
	// carries no usable energy and the conversion diverges.
	inline float AreaPdfToSolidAngle(float pdfArea, float distSq, float cosLight)
	{
		const float c = std::abs(cosLight);
		if (c < 1e-6f)
		{
			return 0.0f;
		}
		return pdfArea * distSq / c;
	}

	// Balance heuristic weight of the strategy with density pdfA against the competing strategy pdfB. Both
	// densities must be in the same measure (solid angle at the shading point).
	inline float BalanceWeight(float pdfA, float pdfB)
	{
		const float sum = pdfA + pdfB;
		return sum > 0.0f ? pdfA / sum : 0.0f;
	}

	// Inclusive normalised prefix sums of non-negative weights (last entry exactly 1). Empty when every weight
	// is zero, which callers treat as "no emitters".
	inline std::vector<float> BuildCdf(const std::vector<float>& weights)
	{
		double total = 0.0;
		for (const float w : weights)
		{
			total += std::max(w, 0.0f);
		}
		if (!(total > 0.0))
		{
			return {};
		}
		std::vector<float> cdf(weights.size());
		double run = 0.0;
		for (size_t i = 0; i < weights.size(); ++i)
		{
			run += std::max(weights[i], 0.0f);
			cdf[i] = static_cast<float>(run / total);
		}
		cdf.back() = 1.0f;
		return cdf;
	}

	struct CdfPick
	{
		uint32_t Index = 0;
		float Pmf = 0.0f;
		float URemapped = 0.0f; // u rescaled to [0,1) inside the chosen bucket, reusable as a fresh sample
	};

	// u in [0,1). Zero-weight buckets have an empty interval and are never picked.
	inline CdfPick PickFromCdf(const std::vector<float>& cdf, float u)
	{
		CdfPick pick;
		if (cdf.empty())
		{
			return pick;
		}
		auto it = std::upper_bound(cdf.begin(), cdf.end(), u);
		if (it == cdf.end())
		{
			it = cdf.end() - 1;
		}
		pick.Index = static_cast<uint32_t>(it - cdf.begin());
		const float lo = pick.Index == 0 ? 0.0f : cdf[pick.Index - 1];
		const float hi = *it;
		pick.Pmf = hi - lo;
		pick.URemapped = pick.Pmf > 0.0f ? std::min((u - lo) / pick.Pmf, 0.99999994f) : 0.0f;
		return pick;
	}
}
