#pragma once

#include <cstddef>
#include <span>

namespace Snowstorm
{
	struct DirectionalShadowCandidate
	{
		bool Enabled = true;
		bool CastShadows = true;
	};

	// Index (in registry order) of the directional the shader reads as DirectionalLights[0]: the first
	// ENABLED one. Returns lights.size() when none is enabled. LightingSystem packs the light array and
	// the raster sun shadow fit both from this rule, so the shadow map can never be fit to a light the
	// shader does not treat as the sun.
	[[nodiscard]] constexpr size_t FindPrimaryDirectional(std::span<const DirectionalShadowCandidate> lights)
	{
		for (size_t i = 0; i < lights.size(); ++i)
		{
			if (lights[i].Enabled)
			{
				return i;
			}
		}
		return lights.size();
	}
}
