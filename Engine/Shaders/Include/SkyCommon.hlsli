// Shared procedural-sky evaluation, used by both the sky background pass (Sky.hlsl) and the IBL
// environment-capture compute shader (IBLCapture.hlsl). Pure function of explicit params (NOT FrameCB)
// so the compute bake — which has no camera FrameCB — can call it with its own inputs. Returns LINEAR
// HDR radiance (pre-tonemap): the capture must convolve physically meaningful values, and the sky pass
// applies its own tonemap/encode afterward.

#ifndef SKY_COMMON_HLSLI
#define SKY_COMMON_HLSLI

// dir          : normalized world-space view ray
// zenith/horizon/ground : environment gradient colors (linear)
// toSun        : normalized direction TOWARD the sun (= -light.Direction); (0,0,0) disables the sun
// sunColor     : sun radiance (light Color * Intensity)
float3 EvaluateSky(float3 dir, float3 zenith, float3 horizon, float3 ground, float3 toSun, float3 sunColor)
{
	const float t = dir.y; // [-1,1]
	float3 sky = (t < 0.0)
	                 ? lerp(horizon, ground, saturate(-t * 3.0)) // quick falloff into the ground band
	                 : lerp(horizon, zenith, saturate(t));

	// Sun disk + soft glow. Skip when toSun is zero-length (no sun).
	if (dot(toSun, toSun) > 1e-6)
	{
		const float d = saturate(dot(dir, toSun));
		const float disk = pow(d, 2000.0);      // tight bright core
		const float glow = pow(d, 64.0) * 0.25; // soft surrounding haze
		sky += (disk + glow) * sunColor;
	}

	return sky;
}

#endif // SKY_COMMON_HLSLI
