#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	constexpr int MAX_DIRECTIONAL_LIGHTS = 4;
	constexpr int MAX_POINT_LIGHTS = 16;
	constexpr int MAX_SPOT_LIGHTS = 16;

	// Hard cap on shadow-casting point (omni) lights. Each costs SIX depth passes (the cube unrolled into
	// 6 atlas tiles), so real engines cap omni shadows aggressively; 2 is the honest budget here. Only this
	// many point lights carry the 6-matrix payload (GPUPointShadow below) -- the rest render unshadowed.
	constexpr int MAX_SHADOW_POINTS = 2;

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
		// Shadow slot: index into the PointShadows array below (0..MAX_SHADOW_POINTS-1), or < 0 when this
		// light casts no shadow (shader skips the sample). Its own 16-byte row so the struct stays aligned.
		int ShadowSlot = -1;
		glm::vec3 ShadowPad = {0, 0, 0};
	};

	// The 6-face shadow payload for ONE shadow-casting point light (cube unrolled into an atlas). Face[f]
	// reprojects world -> that face's 90-degree light clip; Rect[f] (xy = UV offset, zw = UV scale) maps it
	// into that face's tile of the shared point atlas. Face order = +X,-X,+Y,-Y,+Z,-Z (see ShadowPass).
	// Kept separate from GPUPointLight (only MAX_SHADOW_POINTS of these exist) so the 16 point lights don't
	// each carry 480 bytes of matrices they'll never use.
	struct GPUPointShadow
	{
		glm::mat4 Face[6];
		glm::vec4 Rect[6];
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
		// Shadow: ShadowIndex < 0 means this spot casts no shadow (shader skips the sample). Otherwise
		// ShadowViewProj reprojects world -> this spot's light clip, and ShadowAtlasRect (xy = UV offset,
		// zw = UV scale) maps that into the spot's tile of the shared atlas. Kept in 16-byte rows.
		int ShadowIndex = -1;
		glm::vec2 ShadowPad = {0, 0};
		glm::mat4 ShadowViewProj = glm::mat4(1.0f);
		glm::vec4 ShadowAtlasRect = {0, 0, 1, 1};
	};

	// Mirrored field-for-field into the C++ FrameCB (RendererService.cpp, which embeds this struct) and
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

		// Point (omni) shadow payloads: one per shadow-casting point, indexed by GPUPointLight::ShadowSlot.
		// Appended at the tail so no existing offset moves (the FrameCB mirror is order-sensitive). Only the
		// first PointShadowCount entries are valid; slots for non-casting points are never written/read.
		GPUPointShadow PointShadows[MAX_SHADOW_POINTS];
		int PointShadowCount = 0;
		float PointShadowPadding[3] = {0, 0, 0};
	};

	// CPU-side environment values handed to the renderer (RendererService::UploadEnvironment), which
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

		// Value equality so the renderer can detect when the environment changed (e.g. after a scene load)
		// and re-bake IBL, which is otherwise frozen at whatever environment the first bake saw.
		bool operator==(const EnvironmentDataBlock& o) const
		{
			return SkyZenithColor == o.SkyZenithColor && SkyHorizonColor == o.SkyHorizonColor &&
			       GroundColor == o.GroundColor && SkyIntensity == o.SkyIntensity &&
			       DrawProceduralSky == o.DrawProceduralSky;
		}
		bool operator!=(const EnvironmentDataBlock& o) const { return !(*this == o); }
	};
}
