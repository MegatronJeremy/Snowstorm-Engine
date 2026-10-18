#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "Snowstorm/Lighting/LightingUniforms.hpp"

namespace Snowstorm
{
	// The sun as the IBL capture sees it: Lights[0] only, the same source IBLBakePass bakes.
	struct IBLSunKey
	{
		bool Present = false;
		glm::vec3 ToSun = {0, 0, 0};    // unit, surface to sun
		glm::vec3 Radiance = {0, 0, 0}; // Color * Intensity
	};

	inline IBLSunKey MakeIBLSunKey(const LightDataBlock& lights)
	{
		IBLSunKey k;
		if (lights.LightCount <= 0)
			return k;
		const GPUDirectionalLight& sun = lights.Lights[0];
		const float len = glm::length(sun.Direction);
		if (len <= 1e-6f)
			return k;
		k.Present = true;
		k.ToSun = -sun.Direction / len;
		k.Radiance = sun.Color * sun.Intensity;
		return k;
	}

	// Compared against the last BAKED key, never the last frame's, so a slow drift accumulates until it
	// crosses the quantum instead of being swallowed in per-frame steps.
	inline bool SunNeedsRebake(const IBLSunKey& baked, const IBLSunKey& now, float quantumDeg, float radianceRelTol)
	{
		if (baked.Present != now.Present)
			return true;
		if (!now.Present)
			return false;

		const float cosQuantum = std::cos(glm::radians(std::max(quantumDeg, 0.0f)));
		const float cosDelta = glm::clamp(glm::dot(baked.ToSun, now.ToSun), -1.0f, 1.0f);
		if (cosDelta < cosQuantum)
			return true;

		const float ref = std::max(glm::length(baked.Radiance), 1e-6f);
		return glm::length(now.Radiance - baked.Radiance) > radianceRelTol * ref;
	}

	// Degrees between two sun directions; 0 when either is absent.
	inline float SunAngleDeg(const IBLSunKey& a, const IBLSunKey& b)
	{
		if (!a.Present || !b.Present)
			return 0.0f;
		return glm::degrees(std::acos(glm::clamp(glm::dot(a.ToSun, b.ToSun), -1.0f, 1.0f)));
	}

	// Running cost of re-bakes after the first bake: the WaitIdle stall each one pays, and what triggered it.
	struct IBLRebakeStats
	{
		int Count = 0;
		int SunTriggered = 0;
		double TotalStallMs = 0.0;
		double MaxStallMs = 0.0;

		void Record(double stallMs, bool sunTriggered)
		{
			++Count;
			SunTriggered += sunTriggered ? 1 : 0;
			TotalStallMs += stallMs;
			MaxStallMs = std::max(MaxStallMs, stallMs);
		}

		double MeanStallMs() const { return Count > 0 ? TotalStallMs / Count : 0.0; }
	};
}
