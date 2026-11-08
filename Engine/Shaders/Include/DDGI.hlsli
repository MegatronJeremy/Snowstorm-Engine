#ifndef SS_DDGI_HLSLI
#define SS_DDGI_HLSLI

// DDGI probe volume (Majercik et al., JCGT 2019). Mirrors Snowstorm-Core/Source/Snowstorm/Render/DDGIMath.hpp;
// DDGIMathTests pin the shared constants against this file.
// Macros, not static consts: the blend passes size [numthreads] by them.
#define DDGI_IRRADIANCE_TEXELS 6
#define DDGI_DISTANCE_TEXELS 14

struct DDGIVolume
{
	float3 Origin;
	float Spacing;
	uint3 Counts;
	uint TilesPerRow;
};

uint DDGIProbeCount(DDGIVolume v)
{
	return v.Counts.x * v.Counts.y * v.Counts.z;
}

uint3 DDGIProbeCoords(DDGIVolume v, uint index)
{
	return uint3(index % v.Counts.x, (index / v.Counts.x) % v.Counts.y, index / (v.Counts.x * v.Counts.y));
}

uint DDGIProbeIndex(DDGIVolume v, uint3 c)
{
	return c.x + v.Counts.x * (c.y + v.Counts.y * c.z);
}

float3 DDGIProbePosition(DDGIVolume v, uint3 c)
{
	return v.Origin + float3(c) * v.Spacing;
}

uint2 DDGITileOrigin(DDGIVolume v, uint index, uint interior)
{
	return uint2(index % v.TilesPerRow, index / v.TilesPerRow) * (interior + 2);
}

float2 DDGIOctEncode(float3 n)
{
	const float3 a = n / (abs(n.x) + abs(n.y) + abs(n.z));
	if (a.z >= 0.0)
	{
		return a.xy;
	}
	return float2((1.0 - abs(a.y)) * (a.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(a.x)) * (a.y >= 0.0 ? 1.0 : -1.0));
}

float3 DDGIOctDecode(float2 e)
{
	float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
	{
		n.xy = float2((1.0 - abs(e.y)) * (e.x >= 0.0 ? 1.0 : -1.0), (1.0 - abs(e.x)) * (e.y >= 0.0 ? 1.0 : -1.0));
	}
	return normalize(n);
}

float2 DDGITexelOct(uint2 t, uint interior)
{
	return (float2(t) + 0.5) / float(interior) * 2.0 - 1.0;
}

float3 DDGISphericalFibonacci(uint i, uint n)
{
	const float goldenFraction = 0.6180339887;
	const float phi = 6.28318530718 * frac(float(i) * goldenFraction);
	const float cosTheta = 1.0 - (2.0 * float(i) + 1.0) / float(n);
	const float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
	return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float DDGIChebyshevWeight(float mean, float meanSquare, float distance)
{
	if (distance <= mean)
	{
		return 1.0;
	}
	const float variance = abs(meanSquare - mean * mean);
	const float d = distance - mean;
	const float w = variance / max(variance + d * d, 1e-6);
	return max(w * w * w, 0.05);
}

// Atlas UV of direction `dir` in the probe's tile, inside its interior so bilinear taps stay within the border.
float2 DDGIAtlasUV(DDGIVolume v, uint probe, float3 dir, uint interior, float2 atlasSize)
{
	const float2 texel = float2(DDGITileOrigin(v, probe, interior)) + 1.0 + (DDGIOctEncode(dir) * 0.5 + 0.5) * float(interior);
	return texel / atlasSize;
}

// Irradiance (E/PI, the irradiance cube's units) at `position` with surface normal `normal`, seen from direction
// `towardViewer` (unit, from the point toward whoever looks at it). The eight surrounding probes are weighted by
// trilinear position, by whether they sit in front of the surface, and by Chebyshev visibility from their
// distance moments; the position is pushed off the surface first so a probe right behind it is not sampled.
float3 DDGIIrradiance(DDGIVolume v, Texture2D irradianceAtlas, Texture2D distanceAtlas, SamplerState s, float2 irradianceSize,
                      float2 distanceSize, float3 position, float3 normal, float3 towardViewer)
{
	const float3 biased = position + (normal * 0.2 + towardViewer * 0.8) * (0.75 * v.Spacing * 0.3);

	const float3 rel = (biased - v.Origin) / v.Spacing;
	const float3 base = clamp(floor(rel), 0.0, float3(v.Counts) - 2.0);
	const float3 alpha = saturate(rel - base);

	float3 sum = 0.0;
	float weightSum = 0.0;
	[unroll] for (uint i = 0; i < 8; ++i)
	{
		const uint3 offset = uint3(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
		const uint3 coords = uint3(base) + offset;
		const uint probe = DDGIProbeIndex(v, coords);
		const float3 probePos = DDGIProbePosition(v, coords);

		const float3 trilinear3 = lerp(1.0 - alpha, alpha, float3(offset));
		const float trilinear = trilinear3.x * trilinear3.y * trilinear3.z;

		const float3 toProbe = normalize(probePos - position);
		const float facing = (dot(toProbe, normal) + 1.0) * 0.5;
		float weight = facing * facing + 0.2;

		const float3 probeToPoint = biased - probePos;
		const float distance = length(probeToPoint);
		const float2 moments = distanceAtlas.SampleLevel(s, DDGIAtlasUV(v, probe, probeToPoint / max(distance, 1e-4), DDGI_DISTANCE_TEXELS, distanceSize), 0).rg;
		weight *= DDGIChebyshevWeight(moments.x, moments.y, distance);

		// Crush tiny weights so a barely visible probe cannot dominate once the others fall to zero.
		weight = max(weight, 1e-6);
		if (weight < 0.2)
		{
			weight *= weight * weight / (0.2 * 0.2);
		}
		weight *= trilinear;

		sum += irradianceAtlas.SampleLevel(s, DDGIAtlasUV(v, probe, normal, DDGI_IRRADIANCE_TEXELS, irradianceSize), 0).rgb * weight;
		weightSum += weight;
	}
	return weightSum > 0.0 ? sum / weightSum : 0.0;
}

#endif
