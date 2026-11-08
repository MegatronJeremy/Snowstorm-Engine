// Body of DDGIBlendIrradiance.comp / DDGIBlendDistance.comp: one group per probe, one thread per interior texel of
// its tile. Each texel averages the probe's rays around its octahedral direction and blends the result into the
// atlas with the hysteresis. The includer defines DDGI_BLEND_DISTANCE (0 or 1) first.
//
// Irradiance weights rays by the cosine, so the texel is the cosine-weighted mean radiance: irradiance / PI, the
// irradiance cube's convention. Backface rays carry no light and are skipped. Distance raises the cosine to a high
// power so the moments describe a narrow cone, and keeps the mean and mean square for the Chebyshev test.

#include "DDGICommon.hlsli"

Texture2D<float4> RayIn : register(t1, space0);
[[vk::image_format("rgba16f")]] RWTexture2D<float4> AtlasOut : register(u2, space0);

#if DDGI_BLEND_DISTANCE
#define DDGI_BLEND_TEXELS DDGI_DISTANCE_TEXELS
static const float kDistanceSharpness = 50.0;
#else
#define DDGI_BLEND_TEXELS DDGI_IRRADIANCE_TEXELS
#endif
static const uint kTexels = DDGI_BLEND_TEXELS;

[numthreads(DDGI_BLEND_TEXELS, DDGI_BLEND_TEXELS, 1)]
void main(uint3 groupId : SV_GroupID, uint3 local : SV_GroupThreadID)
{
	const DDGIVolume volume = CurrentVolume();
	const uint probe = groupId.x;
	if (probe >= DDGIProbeCount(volume))
	{
		return;
	}

	const float3 texelDir = DDGIOctDecode(DDGITexelOct(local.xy, kTexels));
	float3 sum = float3(0, 0, 0);
	float weightSum = 0.0;
	for (uint ray = 0; ray < RaysPerProbe; ++ray)
	{
		const float4 rayHit = RayIn.Load(int3(ray, probe, 0));
		const float cosine = max(0.0, dot(texelDir, RayDirection(ray)));
#if DDGI_BLEND_DISTANCE
		const float weight = pow(cosine, kDistanceSharpness);
		const float d = min(abs(rayHit.a), DistanceClamp);
		sum.xy += float2(d, d * d) * weight;
#else
		if (rayHit.a < 0.0)
		{
			continue;
		}
		const float weight = cosine;
		sum += rayHit.rgb * weight;
#endif
		weightSum += weight;
	}
	const float3 result = weightSum > 1e-4 ? sum / weightSum : float3(0, 0, 0);

	const uint2 texel = DDGITileOrigin(volume, probe, kTexels) + 1 + local.xy;
	const float3 previous = AtlasOut[texel].rgb;
	const float keep = Reset != 0 ? 0.0 : saturate(Hysteresis);
	AtlasOut[texel] = float4(lerp(result, previous, keep), 1.0);
}
