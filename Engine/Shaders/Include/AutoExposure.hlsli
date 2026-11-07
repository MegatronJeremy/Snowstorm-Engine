#ifndef SS_AUTO_EXPOSURE_HLSLI
#define SS_AUTO_EXPOSURE_HLSLI

// Mirrored by Snowstorm-Core/Source/Snowstorm/Render/AutoExposureMath.hpp, which AutoExposureMathTests compare
// these constants against.
static const uint AE_BINS = 64;
static const float AE_LOG_MIN = -12.0;
static const float AE_LOG_MAX = 8.0;
static const float AE_KEY = 0.18;

uint AutoExposureBin(float luminance)
{
	// The negated comparison sends NaN and non-positive luminance to bin 0.
	if (!(luminance > exp2(AE_LOG_MIN)))
	{
		return 0;
	}
	const float t = saturate((log2(luminance) - AE_LOG_MIN) / (AE_LOG_MAX - AE_LOG_MIN));
	return min(uint(t * float(AE_BINS)), AE_BINS - 1);
}

float AutoExposureBinCentreLog2(uint bin)
{
	return AE_LOG_MIN + (float(bin) + 0.5) / float(AE_BINS) * (AE_LOG_MAX - AE_LOG_MIN);
}

#endif
