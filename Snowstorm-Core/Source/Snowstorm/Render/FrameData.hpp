#pragma once

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Math/Math.hpp"

#include <cstdint>

namespace Snowstorm
{
	// All per-frame inputs the lit/sky passes need, gathered in one place. The PreRender passes populate
	// this each frame (camera → view/camera, LightingSystem → lights, EnvironmentSystem → environment,
	// ShadowPass → Shadow, IBLBakePass → IBL); RendererService::AcquireFrameSet reads it to assemble the
	// GPU FrameCB uniform. This replaces a dozen loose per-feature scalars that used to live directly on
	// RendererService (see #72) — one struct passes fill, instead of one setter + one member per field.
	struct FrameData
	{
		// Camera (set in BeginScene).
		glm::mat4 ViewProjection{1.0f};
		glm::mat4 PrevViewProjection{1.0f}; // last frame's VP — for motion vectors (#44)
		glm::vec3 CameraPosition{0.0f};

		// Scene lighting + environment (uploaded by the PreRender systems).
		LightDataBlock Lights{};
		EnvironmentDataBlock Environment{};

		// Directional-sun shadow (pushed by ShadowPass). ShadowMapIndex 0 = no shadows (fully lit).
		// Resolution feeds the PCF texel-size in AcquireFrameSet.
		struct ShadowBlock
		{
			glm::mat4 LightViewProj{1.0f};
			uint32_t ShadowMapIndex = 0;
			uint32_t ShadowResolution = 2048;
			uint32_t SpotShadowAtlasIndex = 0; // bindless index of the spot shadow atlas (0 = spots unshadowed)
		} Shadow;

		// Baked IBL bindless indices (pushed by IBLBakePass; the maps live in that pass). IrradianceCubeIndex
		// == 0 means IBL is off and DefaultLit falls back to the analytic hemisphere ambient.
		struct IBLBlock
		{
			uint32_t IrradianceCubeIndex = 0;
			uint32_t PrefilteredCubeIndex = 0;
			uint32_t BRDFLutIndex = 0;
			uint32_t PrefilteredMipCount = 0;
		} IBL;
	};
}
