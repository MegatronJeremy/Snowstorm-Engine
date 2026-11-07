// Turns the frame's histogram into a target exposure and moves the persistent exposure toward it. One group of
// AE_BINS threads: each loads and clears one bin (the histogram pass of the next frame accumulates into zeros), then
// thread 0 does the trimmed mean. The maths is AutoExposure::TrimmedMeanLog2 / TargetLog2 / AdaptLog2 in
// AutoExposureMath.hpp.
//
// Exposure texel: .r linear multiplier the tonemap reads, .g its log2 (the adaptation state), .b the target log2.
// Readout repeats (.g, .b) in a host-visible buffer for the CPU, read frames later.

#include "Include/AutoExposure.hlsli"

RWByteAddressBuffer Histogram : register(u0, space0);
[[vk::image_format("rgba32f")]] RWTexture2D<float4> Exposure : register(u1, space0);
RWByteAddressBuffer Readout : register(u2, space0);

[[vk::push_constant]] struct AutoExposureAdaptPush
{
	float LowFraction;
	float HighFraction;
	float MinLog2;
	float MaxLog2;
	float DeltaSeconds;
	float SpeedBrighten;
	float SpeedDarken;
	uint Reset; // no valid previous exposure: take the target
} gPush;

groupshared uint gCount[AE_BINS];

[numthreads(AE_BINS, 1, 1)]
void main(uint groupIndex : SV_GroupIndex)
{
	gCount[groupIndex] = Histogram.Load(groupIndex * 4);
	Histogram.Store(groupIndex * 4, 0);
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex != 0)
	{
		return;
	}

	float total = 0.0;
	for (uint i = 0; i < AE_BINS; ++i)
	{
		total += float(gCount[i]);
	}
	const float from = total * saturate(gPush.LowFraction);
	const float to = total * saturate(max(gPush.LowFraction, gPush.HighFraction));

	float below = 0.0;
	float weighted = 0.0;
	float taken = 0.0;
	for (uint bin = 0; bin < AE_BINS; ++bin)
	{
		const float count = float(gCount[bin]);
		const float take = max(0.0, min(below + count, to) - max(below, from));
		weighted += take * AutoExposureBinCentreLog2(bin);
		taken += take;
		below += count;
	}

	const float current = gPush.Reset != 0 ? 0.0 : Exposure[uint2(0, 0)].g;
	if (taken <= 0.0)
	{
		Exposure[uint2(0, 0)] = float4(exp2(current), current, current, 0.0);
		Readout.Store2(0, asuint(float2(current, current)));
		return;
	}

	const float meanLog2 = weighted / taken;
	const float target = clamp(log2(AE_KEY) - meanLog2, gPush.MinLog2, max(gPush.MinLog2, gPush.MaxLog2));
	float next = target;
	if (gPush.Reset == 0)
	{
		const float speed = target > current ? gPush.SpeedBrighten : gPush.SpeedDarken;
		next = current + (target - current) * (1.0 - exp2(-max(gPush.DeltaSeconds, 0.0) * max(speed, 0.0)));
	}
	Exposure[uint2(0, 0)] = float4(exp2(next), next, target, 0.0);
	Readout.Store2(0, asuint(float2(next, target)));
}
