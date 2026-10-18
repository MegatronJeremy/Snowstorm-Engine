#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <glm/glm.hpp>

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

	// World direction of a directional light: the authored Direction turned by the entity's rotation (the
	// upper 3x3 of its transform; uniform scale cancels in the normalize).
	[[nodiscard]] inline glm::vec3 RotatedLightDirection(const glm::mat3& rotation, const glm::vec3& authored)
	{
		return glm::normalize(rotation * glm::normalize(authored));
	}

	// Raster shadow slot per candidate, in the same order: -1 unshadowed, 0 the sun map (the primary, when
	// it casts), s >= 1 a tile of the directional atlas. At most `maxCasters` lights are shadowed in
	// total (the primary included, whether or not it casts: slot 0 is reserved for it), taken in
	// registry order so a light's shadow never flips with its intensity. `Dropped` counts casters over budget.
	struct DirectionalShadowSlots
	{
		std::vector<int> Slots;
		int Dropped = 0;
	};

	[[nodiscard]] inline DirectionalShadowSlots SelectShadowDirectionals(
	    std::span<const DirectionalShadowCandidate> lights, const int maxCasters, const int maxSecondary)
	{
		DirectionalShadowSlots out;
		out.Slots.assign(lights.size(), -1);
		const size_t primary = FindPrimaryDirectional(lights);
		int nextSlot = 1;
		for (size_t i = primary; i < lights.size(); ++i)
		{
			if (!lights[i].Enabled || !lights[i].CastShadows)
			{
				continue;
			}
			if (i == primary)
			{
				out.Slots[i] = 0;
			}
			else if (nextSlot <= maxCasters - 1 && nextSlot <= maxSecondary)
			{
				out.Slots[i] = nextSlot++;
			}
			else
			{
				++out.Dropped;
			}
		}
		return out;
	}
}
