#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

// Dynamic Diffuse Global Illumination (Majercik et al., JCGT 2019; NVIDIA RTXGI): a grid of probes, each
// storing irradiance and hit-distance moments in a small octahedral tile of a 2D atlas. Engine/Shaders/Include/
// DDGI.hlsli mirrors this file and DDGIMathTests pin the shared rules.
namespace Snowstorm::DDGI
{
	// Interior texels per tile side. Each tile has a one-texel border copied from the opposite octahedral edge so
	// bilinear filtering never reads across probes.
	inline constexpr uint32_t kIrradianceTexels = 6;
	inline constexpr uint32_t kDistanceTexels = 14;

	struct Volume
	{
		glm::vec3 Origin{0.0f}; // probe (0,0,0)
		float Spacing = 1.0f;
		glm::uvec3 Counts{2u};
	};

	inline uint32_t ProbeCount(const Volume& v)
	{
		return v.Counts.x * v.Counts.y * v.Counts.z;
	}

	// The probe grid covering [boundsMin, boundsMax] at `spacing`, centred on the bounds, at least 2 probes per axis
	// so every point has 8 neighbours. Spacing grows until the count fits `maxProbes`.
	inline Volume FitVolume(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float spacing, const uint32_t maxProbes)
	{
		const glm::vec3 extent = glm::max(boundsMax - boundsMin, glm::vec3(0.0f));
		spacing = std::max(spacing, 1e-3f);
		Volume v;
		for (int attempt = 0; attempt < 64; ++attempt)
		{
			v.Spacing = spacing;
			v.Counts = glm::uvec3(glm::max(glm::ceil(extent / spacing) + 1.0f, glm::vec3(2.0f)));
			if (ProbeCount(v) <= std::max(maxProbes, 8u))
			{
				break;
			}
			spacing *= std::max(std::cbrt(static_cast<float>(ProbeCount(v)) / static_cast<float>(std::max(maxProbes, 8u))), 1.01f);
		}
		const glm::vec3 span = glm::vec3(v.Counts - 1u) * v.Spacing;
		v.Origin = (boundsMin + boundsMax) * 0.5f - span * 0.5f;
		return v;
	}

	inline uint32_t ProbeIndex(const Volume& v, const glm::uvec3& c)
	{
		return c.x + v.Counts.x * (c.y + v.Counts.y * c.z);
	}

	inline glm::uvec3 ProbeCoords(const Volume& v, const uint32_t index)
	{
		return {index % v.Counts.x, (index / v.Counts.x) % v.Counts.y, index / (v.Counts.x * v.Counts.y)};
	}

	inline glm::vec3 ProbePosition(const Volume& v, const glm::uvec3& c)
	{
		return v.Origin + glm::vec3(c) * v.Spacing;
	}

	// Tiles sit in a near-square grid of the atlas, so the atlas stays small in both dimensions at any probe count.
	inline uint32_t TilesPerRow(const Volume& v)
	{
		return static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(ProbeCount(v)))));
	}

	inline glm::uvec2 AtlasSize(const Volume& v, const uint32_t interior)
	{
		const uint32_t cols = TilesPerRow(v);
		const uint32_t rows = (ProbeCount(v) + cols - 1) / cols;
		return {cols * (interior + 2), rows * (interior + 2)};
	}

	// Top-left texel of the probe's tile, border included.
	inline glm::uvec2 TileOrigin(const Volume& v, const uint32_t index, const uint32_t interior)
	{
		const uint32_t cols = TilesPerRow(v);
		return {(index % cols) * (interior + 2), (index / cols) * (interior + 2)};
	}

	// Octahedral map of the unit sphere onto [-1,1]^2.
	inline glm::vec2 OctEncode(const glm::vec3& n)
	{
		const glm::vec3 a = n / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
		if (a.z >= 0.0f)
		{
			return {a.x, a.y};
		}
		return {(1.0f - std::abs(a.y)) * (a.x >= 0.0f ? 1.0f : -1.0f), (1.0f - std::abs(a.x)) * (a.y >= 0.0f ? 1.0f : -1.0f)};
	}

	inline glm::vec3 OctDecode(const glm::vec2& e)
	{
		glm::vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
		if (n.z < 0.0f)
		{
			n.x = (1.0f - std::abs(e.y)) * (e.x >= 0.0f ? 1.0f : -1.0f);
			n.y = (1.0f - std::abs(e.x)) * (e.y >= 0.0f ? 1.0f : -1.0f);
		}
		return glm::normalize(n);
	}

	// Octahedral coordinate of interior texel `t` (0-based, border excluded) on a side of `interior` texels.
	inline glm::vec2 TexelOct(const glm::uvec2& t, const uint32_t interior)
	{
		return (glm::vec2(t) + 0.5f) / static_cast<float>(interior) * 2.0f - 1.0f;
	}

	// The tile texel (border included, 0..interior+1) a border texel copies. Crossing an edge of the octahedral
	// square lands on the same edge mirrored, and a corner on the opposite corner.
	inline glm::uvec2 BorderSource(const glm::uvec2& t, const uint32_t interior)
	{
		const uint32_t n = interior;
		const bool left = t.x == 0;
		const bool right = t.x == n + 1;
		const bool top = t.y == 0;
		const bool bottom = t.y == n + 1;
		if ((left || right) && (top || bottom))
		{
			return {left ? n : 1u, top ? n : 1u};
		}
		if (top || bottom)
		{
			return {n + 1 - t.x, top ? 1u : n};
		}
		if (left || right)
		{
			return {left ? 1u : n, n + 1 - t.y};
		}
		return t;
	}

	// Ray i of n evenly spread over the sphere.
	inline glm::vec3 SphericalFibonacci(const uint32_t i, const uint32_t n)
	{
		const float goldenFraction = 0.6180339887f;
		const float phi = 6.28318530718f * (static_cast<float>(i) * goldenFraction - std::floor(static_cast<float>(i) * goldenFraction));
		const float cosTheta = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(n);
		const float sinTheta = std::sqrt(std::clamp(1.0f - cosTheta * cosTheta, 0.0f, 1.0f));
		return {std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
	}

	// The probe cell containing `p`: its lowest corner probe and the trilinear weights along each axis.
	struct Cell
	{
		glm::uvec3 Base{0u};
		glm::vec3 Alpha{0.0f};
	};

	inline Cell CellAt(const Volume& v, const glm::vec3& p)
	{
		const glm::vec3 rel = (p - v.Origin) / v.Spacing;
		const glm::vec3 base = glm::clamp(glm::floor(rel), glm::vec3(0.0f), glm::vec3(v.Counts) - 2.0f);
		return {glm::uvec3(base), glm::clamp(rel - base, glm::vec3(0.0f), glm::vec3(1.0f))};
	}

	// Chebyshev upper bound on the probability that the probe sees a point `distance` away, from the mean and mean
	// square of its hit distances in that direction. Cubed and floored as RTXGI does, which suppresses light leaking
	// through thin walls without zeroing a probe outright.
	inline float ChebyshevWeight(const float mean, const float meanSquare, const float distance)
	{
		if (distance <= mean)
		{
			return 1.0f;
		}
		const float variance = std::abs(meanSquare - mean * mean);
		const float d = distance - mean;
		const float w = variance / std::max(variance + d * d, 1e-6f);
		return std::max(w * w * w, 0.05f);
	}
}
