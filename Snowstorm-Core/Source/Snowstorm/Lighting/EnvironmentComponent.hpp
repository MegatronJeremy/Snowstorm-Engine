#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	// What fills the viewport background. Mirrors the data-driven "background mode" of production engines
	// (Godot WorldEnvironment Background Mode; Unity camera Clear Flags): the scene picks what the sky is,
	// rather than a sky always rendering. SolidColor shows the render target's clear color (no sky pass);
	// ProceduralSky draws the analytic gradient + sun. Future: a Cubemap mode (#35) slots in here.
	enum class BackgroundMode : uint8_t
	{
		SolidColor,
		ProceduralSky,
	};

	// Scene-authored environment definition. One source of truth for the outdoor "fill" light: it drives
	// BOTH the visible procedural sky (Sky.hlsl) AND the scene's hemisphere ambient (DefaultLit.hlsl), so
	// the background and the light on geometry can't drift apart. The sun stays a DirectionalLightComponent
	// (the "key"); this is the dome that wraps everything. Later upgrade seam: IBL (#52) replaces the
	// analytic ambient with an irradiance map captured from these same colors.
	//
	// Presence is opt-in, like Unreal's SkyAtmosphere/SkyLight actors: a scene WITHOUT this component gets
	// no sky pass AND no ambient fill (surfaces lit only by direct lights), so adding the component visibly
	// does something. Defaults match the constants the sky/ambient shaders shipped with.
	struct EnvironmentComponent
	{
		BackgroundMode Background = BackgroundMode::ProceduralSky;

		glm::vec3 SkyZenithColor{0.10f, 0.22f, 0.45f};  // straight up
		glm::vec3 SkyHorizonColor{0.52f, 0.62f, 0.75f}; // at the horizon line
		glm::vec3 GroundColor{0.12f, 0.11f, 0.10f};     // below the horizon
		float SkyIntensity = 1.0f;                      // ambient-fill multiplier
	};
}
