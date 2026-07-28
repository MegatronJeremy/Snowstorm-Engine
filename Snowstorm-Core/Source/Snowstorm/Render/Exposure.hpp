#pragma once

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	// Physical camera exposure (EV100) + metering math. Pure, engine-free helpers -- data in, data out --
	// so they are the single source of truth shared by the exposure plumbing (RendererService fills
	// FrameCB.Exposure from these) and the unit test, the same pattern as HaltonJitter/RotatorMath.
	//
	// Reference model: Frostbite "Moving to PBR" (Lagarde/de Rousiers) and the classic ISO/APEX system.
	// Lights are photometric (sun in lux, point/spot in lumens->candela); the camera maps that real-world
	// magnitude range into [0,1] display range via an exposure multiplier that the tonemap applies as
	// `hdr * Exposure` before ACES. Nothing here touches GPU state.

	// Exposure Value at ISO 100 from the physical camera triangle. N = aperture f-number (f/N),
	// shutterSeconds = exposure time in seconds (e.g. 1/100), iso = film speed. APEX:
	//   EV100 = log2(N^2 / t) - log2(ISO / 100) = log2( N^2 / t * 100 / ISO ).
	// Sunny-16 (f/16, 1/100 s, ISO 100) -> log2(256 * 100) ~= 14.64.
	inline float EV100FromPhysical(const float aperture, const float shutterSeconds, const float iso)
	{
		// Guard degenerate inputs (a 0 aperture/shutter/iso would blow up the log). These can't occur from
		// the clamped editor sliders, but the helper is public so keep it total.
		const float n = std::max(aperture, 1e-4f);
		const float t = std::max(shutterSeconds, 1e-6f);
		const float s = std::max(iso, 1e-4f);
		return std::log2((n * n) / t * 100.0f / s);
	}

	// EV100 from a measured average scene luminance (cd/m^2), the auto-exposure meter. APEX incident/
	// reflected metering: EV100 = log2(L * S / K) with S = 100 (ISO) and the reflected-light calibration
	// constant K = 12.5 (Canon/Nikon). Simplifies to log2(L * 8).
	inline float EV100FromLuminance(const float avgLuminance)
	{
		constexpr float K = 12.5f;
		const float l = std::max(avgLuminance, 1e-4f); // a black frame would give -inf; floor it
		return std::log2(l * 100.0f / K);
	}

	// Exposure compensation: shift the metered/manual EV by a number of stops (each stop doubles/halves
	// the light). Positive EC brightens the image, matching a camera's +/- EV dial -- so we SUBTRACT it
	// from EV100 (a brighter image = metering for a darker scene = lower EV).
	inline float ApplyExposureCompensation(const float ev100, const float exposureCompensationStops)
	{
		return ev100 - exposureCompensationStops;
	}

	// The linear multiplier the tonemap applies (hdr * Exposure). Frostbite saturation-based exposure:
	// maxLuminance = 1.2 * 2^EV100, Exposure = 1 / maxLuminance. A higher EV100 (brighter scene / smaller
	// aperture / faster shutter) yields a smaller multiplier, darkening the captured image -- exactly a
	// real camera stopping down.
	inline float ExposureMultiplierFromEV100(const float ev100)
	{
		const float maxLuminance = 1.2f * std::exp2(ev100);
		return 1.0f / std::max(maxLuminance, 1e-6f);
	}

	// One frame of temporal eye-adaptation: move the current EV toward the target by a frame-rate-
	// independent exponential step (UE eye-adaptation). speed is in "e-folds per second"; dt in seconds.
	// dt<=0 or speed<=0 => snap to target (no adaptation). Pure so it's unit-testable without a clock.
	inline float AdaptEV(const float currentEv, const float targetEv, const float dt, const float speed)
	{
		if (dt <= 0.0f || speed <= 0.0f)
		{
			return targetEv;
		}
		const float blend = 1.0f - std::exp(-dt * speed);
		return currentEv + (targetEv - currentEv) * blend;
	}
}
