#pragma once

#include "LightingUniforms.hpp"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Snowstorm
{
	// Component values to GPU rows, with no ECS or renderer: LightingSystem supplies the world-space inputs
	// and the shadow slot it assigned, these only encode them.

	// `direction` is the world direction already turned by RotatedLightDirection; the shader needs it unit length.
	[[nodiscard]] inline GPUDirectionalLight PackDirectionalLight(
	    const glm::vec3& direction, const float intensity, const glm::vec3& color, const int shadowSlot)
	{
		return {.Direction = glm::normalize(direction), .Intensity = intensity, .Color = color, .ShadowSlot = shadowSlot};
	}

	[[nodiscard]] inline GPUPointLight PackPointLight(
	    const glm::vec3& position, const float range, const glm::vec3& color, const float intensity, const int shadowSlot)
	{
		return {.Position = position,
		        .Range = range,
		        .Color = color,
		        .Intensity = intensity,
		        .ShadowSlot = shadowSlot,
		        .ShadowPad = {0, 0, 0}};
	}

	// Spot forward is the entity's -Z axis (upper 3x3 of its transform; scale cancels in the normalize).
	[[nodiscard]] inline glm::vec3 SpotForward(const glm::mat3& rotation)
	{
		return glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
	}

	// Outer is clamped to at least inner, so cos(inner) >= cos(outer) and the falloff denominator stays positive.
	[[nodiscard]] inline float SpotOuterRadians(const float innerDeg, const float outerDeg)
	{
		return glm::radians(std::max(outerDeg, innerDeg));
	}

	[[nodiscard]] inline GPUSpotLight PackSpotLight(
	    const glm::vec3& position, const float range, const glm::vec3& color, const float intensity,
	    const glm::vec3& forward, const float innerDeg, const float outerDeg, const int shadowIndex,
	    const glm::mat4& shadowViewProj, const glm::vec4& shadowAtlasRect)
	{
		return {.Position = position,
		        .Range = range,
		        .Color = color,
		        .Intensity = intensity,
		        .Direction = forward,
		        .CosInner = std::cos(glm::radians(innerDeg)),
		        .CosOuter = std::cos(SpotOuterRadians(innerDeg, outerDeg)),
		        .ShadowIndex = shadowIndex,
		        .ShadowPad = {0, 0},
		        .ShadowViewProj = shadowViewProj,
		        .ShadowAtlasRect = shadowAtlasRect};
	}

	// The arrays are fixed-size and mirrored on the GPU, so a full one drops the light instead of writing past it.
	[[nodiscard]] constexpr bool DirectionalFull(const LightDataBlock& b)
	{
		return b.LightCount >= MAX_DIRECTIONAL_LIGHTS;
	}
	[[nodiscard]] constexpr bool PointFull(const LightDataBlock& b)
	{
		return b.PointCount >= MAX_POINT_LIGHTS;
	}
	[[nodiscard]] constexpr bool SpotFull(const LightDataBlock& b)
	{
		return b.SpotCount >= MAX_SPOT_LIGHTS;
	}
}
