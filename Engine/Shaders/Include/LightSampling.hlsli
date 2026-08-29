// LightSampling.hlsli: solid-angle sampling of a light's visible disk, shared by the path tracer, the
// inline forward RT shadows (DefaultLit) and the stochastic aggregate-shadow pass (Shadow.comp), so that
// render.shadow.sun_angle_deg / render.shadow.source_radius denote the same cone in all three. The
// real-time paths previously jittered within a tangent-plane DISK (uniform by AREA), which agrees with a
// solid-angle cone only in the small-angle limit and treats the same CVar value as a tangent where the
// path tracer treats it as a sine.
//
// Pure functions only, no resource declarations, so including this cannot perturb a pipeline's reflected
// binding layout (see the space1 note in Engine.hlsli).

#ifndef SNOWSTORM_LIGHT_SAMPLING_HLSLI
#define SNOWSTORM_LIGHT_SAMPLING_HLSLI

static const float SS_TWO_PI = 6.28318530718;

// Interleaved-gradient noise in [0,1), FIXED per pixel: its power spectrum is blue-ish in screen space
// (Jimenez), so neighbouring pixels get well-separated values that a spatial filter (a-trous, or TAA's
// neighbourhood clamp) averages cleanly. Spatial base for the sampler below.
float IGN(uint2 px)
{
	return frac(52.9829189 * frac(0.06711056 * float(px.x) + 0.00583715 * float(px.y)));
}

// Spatiotemporal blue-noise sample in [0,1): the fixed IGN spatial pattern advanced by a golden-ratio (R1
// low-discrepancy) increment per FRAME and decorrelated per DIMENSION. Animating blue noise by the golden
// ratio keeps the screen-space blue-noise property while making successive frames low-discrepancy IN TIME
// (Wolfe, "Animating Noise for Integration Over Time"; MegaLights/NRD both stress blue + low-discrepancy at
// 1-4 rpp). Translating the IGN input by frame instead re-rolls the whole spatial pattern every frame, which
// is white in time and defeats temporal accumulation. `dim` separates the independent draws a single pixel
// makes in one frame (per light, per sample, per axis).
// 0.6180339887 = 1/phi (temporal); 0.7548776662 = the plastic-number R2 additive constant (per-dimension).
float STBN(uint2 px, uint frame, uint dim)
{
	// Wrap the frame to a 64-frame period: keeps float(frame) exact over arbitrarily long runs AND matches NRD's
	// "limited animated frames" guidance for blue noise. dim stays O(100) so it is exact too.
	return frac(IGN(px) + float(frame & 63u) * 0.61803398875 + float(dim) * 0.75487766624);
}

// Branchless orthonormal basis (Duff et al. 2017, "Building an Orthonormal Basis, Revisited").
void OnbFromNormal(float3 n, out float3 t, out float3 b)
{
	const float sgn = n.z >= 0.0 ? 1.0 : -1.0;
	const float a = -1.0 / (sgn + n.z);
	const float bb = n.x * n.y * a;
	t = float3(1.0 + sgn * n.x * n.x * a, sgn * bb, -sgn * n.x);
	b = float3(bb, sgn + n.y * n.y * a, -n.y);
}

float3 ToWorld(float3 v, float3 n)
{
	float3 t, b;
	OnbFromNormal(n, t, b);
	return v.x * t + v.y * b + v.z * n;
}

// Uniformly sample a direction inside the cone of half-angle acos(cosThetaMax) around `axis`. Giving the
// sun a FINITE angular size is what stops its specular reflection collapsing to one infinitely bright
// pixel on smooth surfaces. cosThetaMax == 1 collapses back to the exact axis (a delta light).
float3 SampleCone(float3 axis, float cosThetaMax, float u1, float u2)
{
	const float cosTheta = 1.0 - u1 * (1.0 - cosThetaMax);
	const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
	const float phi = SS_TWO_PI * u2;
	const float3 local = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	return normalize(ToWorld(local, axis));
}

// cos of the half-angle a sphere of radius `radius` subtends at distance `dist`. The saturate bounds the
// near field: at dist < radius the shading point is inside the sphere, and an unclamped ratio would drive
// the half-angle past 90 degrees and scatter samples behind the surface.
float LightConeCos(float radius, float dist)
{
	const float sinHalf = saturate(radius / max(dist, 1e-4));
	return sqrt(max(0.0, 1.0 - sinHalf * sinHalf));
}

#endif
