#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	constexpr int MAX_DIRECTIONAL_LIGHTS = 4;
	constexpr int MAX_POINT_LIGHTS = 16;
	constexpr int MAX_SPOT_LIGHTS = 16;

	struct GPUDirectionalLight
	{
		glm::vec3 Direction;
		float Intensity;
		glm::vec3 Color;
		float Padding = 0.0f;
	};

	// Positional light GPU layouts. Each is whole 16-byte rows (std140/cbuffer packing). Position and,
	// for spot, Direction are baked in world space by LightingSystem from the entity transform. Cone
	// angles are stored as cos(angle) so the shader compares against dot() directly (no per-fragment trig).
	struct GPUPointLight
	{
		glm::vec3 Position;
		float Range;
		glm::vec3 Color;
		float Intensity;
	};

	struct GPUSpotLight
	{
		glm::vec3 Position;
		float Range;
		glm::vec3 Color;
		float Intensity;
		glm::vec3 Direction;
		float CosInner;
		float CosOuter;
		glm::vec3 Padding = {0, 0, 0};
	};

	// Mirrored field-for-field into the C++ FrameCB (RendererSingleton.cpp, which embeds this struct) and
	// the HLSL cbuffer FrameCB (Engine.hlsli). New arrays are appended AFTER the directional block so the
	// existing directional offsets don't move. Keep all three in lockstep -- a layout drift silently
	// corrupts lighting (the "FrameCB mirror trap").
	struct LightDataBlock
	{
		GPUDirectionalLight Lights[MAX_DIRECTIONAL_LIGHTS];
		int LightCount = 0;
		float Padding[3] = {0, 0, 0};

		GPUPointLight PointLights[MAX_POINT_LIGHTS];
		int PointCount = 0;
		float PointPadding[3] = {0, 0, 0};

		GPUSpotLight SpotLights[MAX_SPOT_LIGHTS];
		int SpotCount = 0;
		float SpotPadding[3] = {0, 0, 0};
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
