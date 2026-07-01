#include "Include/Engine.hlsli"

// Mandelbrot fragment stage: renders the fractal into a mesh quad using per-instance Extras0 params.
// Pairs with the shared Mesh.vert.hlsl (it ignores the normal/tangent that VS fills). Was the
// fragment half of the old combined Mandelbrot.hlsl.


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

	// Map texcoord to complex plane
	const float2 c = center + (i.TexCoord - 0.5) * zoom;

	float2 z = float2(0.0, 0.0);
	int iter = 0;

	[loop] for (iter = 0; iter < maxIter; ++iter)
	{
		if (dot(z, z) > 4.0)
			break;
		z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
	}

	const float colorFactor = (float)iter / (float)maxIter;
	const float3 color = hsv_to_rgb(colorFactor, 1.0, 1.0);

	if (iter == maxIter)
		return float4(0.0, 0.0, 0.0, 1.0);

	return float4(color, 1.0);
}