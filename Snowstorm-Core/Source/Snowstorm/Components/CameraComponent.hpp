#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct CameraComponent
	{
		enum class ProjectionType : uint8_t
		{
			Perspective = 0,
			Orthographic = 1
		};

		// Exposure metering mode: Manual = fixed EV from the physical camera triangle below; Auto = meter
		// the scene's average luminance and adapt EV over time (Unity Physical Camera / Unreal exposure).
		enum class ExposureMode : uint8_t
		{
			Manual = 0,
			Auto = 1
		};

		ProjectionType Projection = ProjectionType::Perspective;

		// Perspective
		float PerspectiveFOV = 0.785398f; // radians
		// Depth precision in a perspective projection is dominated by the near plane (precision ∝ 1/near).
		// near=0.01 with far=500-1000 makes the far/near ratio so large that distant coplanar surfaces
		// collapse to the same depth and z-fight. 0.1 is close enough for any sane scene and ~10x the
		// usable precision.
		float PerspectiveNear = 0.1f;
		float PerspectiveFar = 500.0f;

		// Ortho
		float OrthographicSize = 10.0f;
		float OrthographicNear = -10.0f;
		float OrthographicFar = 10.0f;

		bool Primary = true;
		bool FixedAspectRatio = false;
		float AspectRatio = 16.0f / 9.0f; // used only if FixedAspectRatio

		// --- Physical camera exposure (Snowstorm/Render/Exposure.hpp does the math). Lights are photometric
		// (sun in lux, point/spot in lumens); these map that real-world range into display range. Serialized
		// per-camera like Unity's Physical Camera / an Unreal PostProcess volume.
		ExposureMode Exposure = ExposureMode::Manual;

		// Manual mode: the classic camera triangle -> a fixed EV100. Defaults are a dim-interior exposure
		// (f/5.6, 1/60 s, ISO 400 ~= EV100 9.7) rather than sunny-16, since the reference scene is indoors.
		float Aperture = 5.6f;           // f-number (f/N); larger = smaller opening = darker
		float ShutterSpeed = 1.0f / 60.0f; // seconds; faster = darker
		float ISO = 400.0f;              // film speed; higher = brighter
		float ExposureCompensation = 0.0f; // +/- stops on top of the metered/manual EV (brightens when +)

		// Auto mode: meter scene luminance -> EV100, clamped to [MinEV, MaxEV], approached at AdaptationSpeed
		// (e-folds/sec) for smooth eye-adaptation. Ignored in Manual mode.
		float MinEV = -2.0f;
		float MaxEV = 16.0f;
		float AdaptationSpeed = 1.5f;
	};
}
