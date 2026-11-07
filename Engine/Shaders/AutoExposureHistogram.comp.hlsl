// Log2-luminance histogram of the linear HDR scene colour, accumulated into Histogram (AE_BINS uints). Each group
// bins into groupshared memory first, so global atomics drop to one per non-empty bin per group.

#include "Include/AutoExposure.hlsli"

Texture2D<float4> SceneColor : register(t0, space0);
RWByteAddressBuffer Histogram : register(u1, space0);

[[vk::push_constant]] struct AutoExposureHistogramPush
{
	uint2 Size;
} gPush;

groupshared uint gBins[AE_BINS];

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	if (groupIndex < AE_BINS)
	{
		gBins[groupIndex] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	if (all(id.xy < gPush.Size))
	{
		const float3 c = SceneColor.Load(int3(id.xy, 0)).rgb;
		uint ignored;
		InterlockedAdd(gBins[AutoExposureBin(dot(c, float3(0.2126, 0.7152, 0.0722)))], 1, ignored);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < AE_BINS && gBins[groupIndex] != 0)
	{
		Histogram.InterlockedAdd(groupIndex * 4, gBins[groupIndex]);
	}
}
