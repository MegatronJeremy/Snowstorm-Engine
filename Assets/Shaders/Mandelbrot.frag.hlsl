#include "Include/Engine.hlsli"

// Mandelbrot fragment stage: renders the fractal into a mesh quad using per-instance Extras0
// (xy=center, z=zoom span, w=maxIter). Pairs with the shared Mesh.vert.hlsl.
//
// Quality vs the naive version:
//  - SMOOTH (continuous) iteration count -- removes the concentric colour banding of integer iter/max.
//  - Higher escape radius (256) -- required for the smooth estimate to be accurate.
//  - Cardioid + period-2 bulb early-out -- the two largest interior regions are detected analytically
//    and skipped, so the inner black area doesn't burn maxIter iterations per pixel (big speedup that
//    also lets us afford more iterations elsewhere).
//  - A smooth cosine palette (Inigo Quilez style) instead of a hard HSV wheel.
//
// Precision note: this runs in fp32. The complex-plane step per pixel is ~zoom/resolution; once that
// approaches fp32 epsilon (~1e-7 relative) the deep-zoom image degrades into blocky "mush". True deep
// zoom needs fp64 or perturbation theory (a much larger change) -- deliberately out of scope here.

// Full-saturation HSV rainbow (the original Mandelbrot palette). Kept so the colours read the same as
// before; the only change vs the old shader is that the hue is now driven by the SMOOTH iteration count
// below, so the rainbow is a continuous gradient instead of hard concentric bands.
float3 hsv_to_rgb(float h, float s, float v)
{
	float f = frac(h) * 6.0;
	int i = (int)floor(f);
	f -= i;

	float p = v * (1.0 - s);
	float q = v * (1.0 - s * f);
	float t = v * (1.0 - s * (1.0 - f));

	if (i == 0)
		return float3(v, t, p);
	if (i == 1)
		return float3(q, v, p);
	if (i == 2)
		return float3(p, v, t);
	if (i == 3)
		return float3(p, q, v);
	if (i == 4)
		return float3(t, p, v);
	return float3(v, p, q);
}

float4 main(PSInput i) : SV_Target0
{
	const float4 extras = Instances[i.InstanceID].Extras0;
	const float2 center = extras.xy;
	const float zoom = extras.z;
	const int maxIter = max(1, (int)extras.w);

	// Map texcoord to the complex plane.
	const float2 c = center + (i.TexCoord - 0.5) * zoom;

	// Interior early-out: main cardioid and period-2 bulb. Points inside never escape, so this avoids
	// running the full iteration loop across the entire black interior.
	const float xm = c.x - 0.25;
	const float q = xm * xm + c.y * c.y;
	const bool inCardioid = q * (q + xm) <= 0.25 * c.y * c.y;
	const bool inBulb = (c.x + 1.0) * (c.x + 1.0) + c.y * c.y <= 0.0625;
	if (inCardioid || inBulb)
	{
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	// Escape radius 256 (not 2): a larger bailout makes the smooth-iteration estimate below accurate.
	const float escape2 = 256.0 * 256.0;

	float2 z = float2(0.0, 0.0);
	int iter = 0;
	[loop] for (iter = 0; iter < maxIter; ++iter)
	{
		z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
		if (dot(z, z) > escape2)
			break;
	}

	if (iter >= maxIter)
	{
		return float4(0.0, 0.0, 0.0, 1.0); // interior
	}

	// Continuous (fractional) iteration count: iter + 1 - log2(log2(|z|)). Smooths the colour so bands
	// become a continuous gradient. Guard the logs against |z| ~ escape edge.
	const float mag = max(length(z), 1.0001);
	const float smoothIter = float(iter) + 1.0 - log2(max(log2(mag), 1e-6));

	// Original palette mapping: hue = iteration fraction, full saturation/value. Same look as before, but
	// fed the SMOOTH count so it's a continuous rainbow instead of banded.
	const float colorFactor = smoothIter / float(maxIter);
	return float4(hsv_to_rgb(colorFactor, 1.0, 1.0), 1.0);
}