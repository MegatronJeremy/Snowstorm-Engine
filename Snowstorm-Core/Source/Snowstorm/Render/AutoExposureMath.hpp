#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

// Histogram eye adaptation (Unreal's Auto Exposure Histogram model). Engine/Shaders/Include/AutoExposure.hlsli
// and AutoExposureAdapt.comp.hlsl compute the same thing on the GPU; AutoExposureMathTests pin the shared
// constants against the shader copy. Exposure is handled as log2 of the linear multiplier the tonemap applies.
namespace Snowstorm::AutoExposure
{
	inline constexpr uint32_t kBins = 64;
	// log2 scene luminance spanned by the histogram: 20 stops, 0.3125 stops per bin. Darker pixels land in bin 0,
	// brighter in the last bin.
	inline constexpr float kLogMin = -12.0f;
	inline constexpr float kLogMax = 8.0f;
	// The metered luminance is mapped to this middle grey.
	inline constexpr float kKey = 0.18f;

	using Histogram = std::array<uint32_t, kBins>;

	inline uint32_t BinOf(const float luminance)
	{
		// The negated comparison sends NaN and non-positive luminance to bin 0.
		if (!(luminance > std::exp2(kLogMin)))
		{
			return 0;
		}
		const float t = std::clamp((std::log2(luminance) - kLogMin) / (kLogMax - kLogMin), 0.0f, 1.0f);
		return std::min(static_cast<uint32_t>(t * static_cast<float>(kBins)), kBins - 1);
	}

	inline float BinCentreLog2(const uint32_t bin)
	{
		return kLogMin + (static_cast<float>(bin) + 0.5f) / static_cast<float>(kBins) * (kLogMax - kLogMin);
	}

	// Mean log2 luminance of the pixels ranked between the `low` and `high` fractions of the histogram mass, so the
	// darkest and the brightest outliers do not drive exposure. A bin straddling a bound contributes the part
	// inside it. Empty histogram: no value.
	inline std::optional<float> TrimmedMeanLog2(const Histogram& histogram, const float low, const float high)
	{
		double total = 0.0;
		for (const uint32_t count : histogram)
		{
			total += count;
		}
		const double from = total * std::clamp(low, 0.0f, 1.0f);
		const double to = total * std::clamp(std::max(low, high), 0.0f, 1.0f);

		double below = 0.0;
		double weighted = 0.0;
		double taken = 0.0;
		for (uint32_t bin = 0; bin < kBins; ++bin)
		{
			const double count = histogram[bin];
			const double take = std::max(0.0, std::min(below + count, to) - std::max(below, from));
			weighted += take * BinCentreLog2(bin);
			taken += take;
			below += count;
		}
		if (taken <= 0.0)
		{
			return std::nullopt;
		}
		return static_cast<float>(weighted / taken);
	}

	inline float TargetLog2(const float meanLog2, const float minLog2, const float maxLog2)
	{
		return std::clamp(std::log2(kKey) - meanLog2, minLog2, std::max(minLog2, maxLog2));
	}

	// Exponential approach in log2 space: each second closes all but 2^-speed of the gap. Brightening (the scene got
	// darker) and darkening run at their own rates, since the eye adapts to light faster than to dark.
	inline float AdaptLog2(const float current, const float target, const float deltaSeconds, const float speedBrighten,
	                       const float speedDarken)
	{
		const float speed = target > current ? speedBrighten : speedDarken;
		const float factor = 1.0f - std::exp2(-std::max(deltaSeconds, 0.0f) * std::max(speed, 0.0f));
		return current + (target - current) * factor;
	}
}
