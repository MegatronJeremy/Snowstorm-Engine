#pragma once

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Math/Math.hpp"

#include <algorithm>
#include <cstdint>

namespace Snowstorm
{
	// Local (point + spot) lights packed for the secondary-hit shading shared by the GI and Reflection
	// compute passes (RTHitShading.hlsli). Both passes MUST pack identically: they feed the same shader
	// code, so a drift between two hand-rolled copies would show up as GI and reflections disagreeing about
	// the same light, which is far harder to spot than a compile error. Hence one packer, one size constant.
	//
	// Point and spot share ONE array so the shader's stochastic light pick is a single uniform draw over
	// `count` instead of a two-stage point-then-spot choice. A point light is marked by CosOuter = -2, a
	// value no real cone can produce (cos is bounded by [-1, 1]).
	constexpr uint32_t kRTHitMaxLights = 16;
	constexpr float kRTHitPointSentinel = -2.0f;

	// Writes up to kRTHitMaxLights entries into the three parallel float4 arrays and returns the count.
	// Layout (mirrored by RTHitShading.hlsli):
	//   posRange[i] = (position, range)
	//   color[i]    = (color * intensity, cosInner)   radiance pre-attenuation; the shader applies falloff
	//   dirCos[i]   = (spot direction, cosOuter)      cosOuter == kRTHitPointSentinel => point light
	inline uint32_t PackRTHitLights(const LightDataBlock& lights, glm::vec4* posRange, glm::vec4* color, glm::vec4* dirCos)
	{
		uint32_t n = 0;

		const uint32_t pointCount = static_cast<uint32_t>(std::clamp(lights.PointCount, 0, MAX_POINT_LIGHTS));
		for (uint32_t i = 0; i < pointCount && n < kRTHitMaxLights; ++i, ++n)
		{
			const GPUPointLight& L = lights.PointLights[i];
			posRange[n] = glm::vec4(L.Position, L.Range);
			color[n] = glm::vec4(L.Color * L.Intensity, 0.0f);
			dirCos[n] = glm::vec4(0.0f, 0.0f, 0.0f, kRTHitPointSentinel);
		}

		const uint32_t spotCount = static_cast<uint32_t>(std::clamp(lights.SpotCount, 0, MAX_SPOT_LIGHTS));
		for (uint32_t i = 0; i < spotCount && n < kRTHitMaxLights; ++i, ++n)
		{
			const GPUSpotLight& L = lights.SpotLights[i];
			posRange[n] = glm::vec4(L.Position, L.Range);
			color[n] = glm::vec4(L.Color * L.Intensity, L.CosInner);
			dirCos[n] = glm::vec4(L.Direction, L.CosOuter);
		}

		return n;
	}
}
