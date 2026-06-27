#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	constexpr int MAX_DIRECTIONAL_LIGHTS = 4;

	struct GPUDirectionalLight
	{
		glm::vec3 Direction;
		float Intensity;
		glm::vec3 Color;
		float Padding = 0.0f;
	};

	struct LightDataBlock
	{
		GPUDirectionalLight Lights[MAX_DIRECTIONAL_LIGHTS];
		int LightCount = 0;
		float Padding[3] = {0, 0, 0};
	};

	// CPU-side environment values handed to the renderer (RendererSingleton::UploadEnvironment), which
	// folds the colors into FrameCB and uses DrawProceduralSky to decide whether to run the sky pass.
	// Default state = inactive (no EnvironmentComponent in the scene): no sky, and zeroed colors so the
	// ambient term contributes nothing (surfaces lit only by direct lights), matching engines that give
	// no ambient without an explicit sky/environment.
	struct EnvironmentDataBlock
	{
		glm::vec3 SkyZenithColor{0.0f};
		glm::vec3 SkyHorizonColor{0.0f};
		glm::vec3 GroundColor{0.0f};
		float SkyIntensity = 0.0f;

		// Whether the procedural sky background pass should run (BackgroundMode::ProceduralSky with an
		// active component). SolidColor / no component => false => the render target's clear color shows.
		bool DrawProceduralSky = false;
	};
}
