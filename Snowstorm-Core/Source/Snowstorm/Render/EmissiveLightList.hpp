#pragma once

#include "Snowstorm/Render/EmissiveLightMath.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace Snowstorm::EmissiveLightList
{
	// One world-space triangle of an emissive instance, as PathTrace.comp.hlsl reads it by device address
	// (LoadEmissiveTri). 64 bytes; offsets are the contract.
	struct GpuTriangle
	{
		float P0[3];
		uint32_t Instance; // TLAS instance index (GeoRecord row)
		float P1[3];
		uint32_t Prim; // primitive index inside the instance's mesh
		float P2[3];
		float Cdf;  // inclusive normalised prefix sum over the whole list
		float Pmf;  // pick probability, Cdf[i] - Cdf[i-1]
		float Area; // world-space area; the area-measure density of a uniform sample is Pmf / Area
		float Pad[2];
	};
	static_assert(sizeof(GpuTriangle) == 64, "GpuTriangle must be 64 bytes to match the HLSL RawBufferLoad offsets");

	// Above this the list is dropped and the path tracer falls back to BSDF-only emission, which is unbiased
	// but noisy: a truncated list would bias the estimate.
	constexpr size_t kMaxTriangles = size_t{1} << 20;

	struct InstanceInput
	{
		uint32_t Instance = 0;
		glm::mat4 Model{1.0f};
		glm::vec3 Emissive{0.0f}; // material emissive factor
		const glm::vec3* Positions = nullptr;
		size_t PositionCount = 0;
		const uint32_t* Indices = nullptr;
		size_t IndexCount = 0;
	};

	struct Result
	{
		std::vector<GpuTriangle> Triangles;
		std::vector<uint32_t> FirstTriangle; // per input: index + 1 of its first triangle, 0 = not in the list
		bool Truncated = false;              // too many triangles: Triangles is empty, nothing is sampled
	};

	inline float Luminance(const glm::vec3& c)
	{
		return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	// Every triangle of an instance with a non-black emissive factor enters the list (zero-area ones with weight 0),
	// so the entry of triangle `prim` of an instance is FirstTriangle - 1 + prim and the shader can find the light
	// pdf of a BSDF-sampled emissive hit without a search. Weight is world area times emissive luminance: a textured
	// emitter is sampled by its factor only, which stays unbiased because the pmf is positive wherever the factor is.
	inline Result Build(const std::vector<InstanceInput>& inputs)
	{
		Result r;
		r.FirstTriangle.assign(inputs.size(), 0u);

		size_t total = 0;
		for (const InstanceInput& in : inputs)
		{
			if (Luminance(in.Emissive) > 0.0f && in.Positions && in.Indices)
			{
				total += in.IndexCount / 3;
			}
		}
		if (total > kMaxTriangles)
		{
			r.Truncated = true;
			return r;
		}

		r.Triangles.reserve(total);
		std::vector<float> weights;
		weights.reserve(total);
		for (size_t n = 0; n < inputs.size(); ++n)
		{
			const InstanceInput& in = inputs[n];
			const float lum = Luminance(in.Emissive);
			if (!(lum > 0.0f) || !in.Positions || !in.Indices)
			{
				continue;
			}
			r.FirstTriangle[n] = static_cast<uint32_t>(r.Triangles.size()) + 1u;
			for (size_t t = 0; t + 2 < in.IndexCount; t += 3)
			{
				glm::vec3 p[3];
				bool valid = true;
				for (int k = 0; k < 3; ++k)
				{
					const uint32_t idx = in.Indices[t + k];
					if (idx >= in.PositionCount)
					{
						valid = false;
						p[k] = glm::vec3(0.0f);
						continue;
					}
					p[k] = glm::vec3(in.Model * glm::vec4(in.Positions[idx], 1.0f));
				}
				const float area = valid ? EmissiveLightMath::TriangleArea(p[0], p[1], p[2]) : 0.0f;
				GpuTriangle g{};
				for (int k = 0; k < 3; ++k)
				{
					float* dst = k == 0 ? g.P0 : (k == 1 ? g.P1 : g.P2);
					dst[0] = p[k].x;
					dst[1] = p[k].y;
					dst[2] = p[k].z;
				}
				g.Instance = in.Instance;
				g.Prim = static_cast<uint32_t>(t / 3);
				g.Area = area;
				r.Triangles.push_back(g);
				weights.push_back(area * lum);
			}
		}

		const std::vector<float> cdf = EmissiveLightMath::BuildCdf(weights);
		if (cdf.empty())
		{
			r.Triangles.clear();
			r.FirstTriangle.assign(inputs.size(), 0u);
			return r;
		}
		for (size_t i = 0; i < r.Triangles.size(); ++i)
		{
			r.Triangles[i].Cdf = cdf[i];
			r.Triangles[i].Pmf = cdf[i] - (i == 0 ? 0.0f : cdf[i - 1]);
		}
		return r;
	}
}
