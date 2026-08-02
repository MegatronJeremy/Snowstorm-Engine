// PSNR + canonical windowed mean SSIM (#45, windowing #96). Reads the upscaled and ground-truth LDR
// present images (both full-res, same resolution — see CreatePresentTarget). One thread per pixel;
// values are the perceptual [0,1] tonemapped sRGB the viewer sees, converted to Rec.709 luma (the
// standard channel for grayscale PSNR/SSIM).
//
// Accumulated (over N pixels), in fixed-point via InterlockedAdd (HLSL has no float atomics):
//   [0] sum( (a-b)^2 )      -> MSE -> PSNR = 10*log10(1/MSE)   (genuinely global — correct for MSE)
//   [1] sum( SSIM_window )  -> MEAN SSIM = sum / N             (#96: windowed, not whole-image)
//
// #96: SSIM is the Wang et al. MEAN SSIM — each thread computes SSIM over an 11x11 Gaussian window
// (sigma=1.5) centered on its pixel and accumulates that scalar; the CPU divides by the counted
// pixels. The previous GLOBAL SSIM used one whole-image statistic — it can't see local structural
// error (a blurry region and a sharp region cancel in the global variance), which is exactly what
// SSIM is meant to localize. The Gaussian window + border crop match skimage's
// structural_similarity(gaussian_weights=True), so the numbers are publication-comparable.

Texture2D<float4> UpscaledTex : register(t0, space0);
Texture2D<float4> GroundTruthTex : register(t1, space0);
RWByteAddressBuffer Sums : register(u2, space0); // [0] SSE, [1] sum(local SSIM)

cbuffer MetricsCB : register(b3, space0)
{
	uint2 Resolution;
	float FixedScale;
	float _Pad;
};

float Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Normalized 11-tap Gaussian kernel, sigma=1.5. The separable product yields the canonical
// 11x11 SSIM window used by skimage when gaussian_weights=true and truncate=3.5.
static const float Gaussian11[11] = {
	0.0010283801, 0.0075987581, 0.0360007721, 0.1093606870, 0.2130055377, 0.2660117249,
	0.2130055377, 0.1093606870, 0.0360007721, 0.0075987581, 0.0010283801
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Resolution.x || id.y >= Resolution.y)
	{
		return;
	}

	const int2 pixel = int2(id.xy);
	const float a = Luma(UpscaledTex.Load(int3(pixel, 0)).rgb);
	const float b = Luma(GroundTruthTex.Load(int3(pixel, 0)).rgb);
	const float difference = a - b;

	// Match reference implementations: the reported mean uses only pixels whose complete 11x11
	// window lies inside the image (skimage crops the five-pixel filter border before averaging).
	if (any(pixel < 5) || any(pixel >= int2(Resolution) - 5))
	{
		Sums.InterlockedAdd(0, (uint)(difference * difference * FixedScale + 0.5));
		return;
	}

	float meanA = 0.0;
	float meanB = 0.0;
	float meanAA = 0.0;
	float meanBB = 0.0;
	float meanAB = 0.0;
	for (int y = -5; y <= 5; ++y)
	{
		for (int x = -5; x <= 5; ++x)
		{
			const int2 samplePixel = pixel + int2(x, y);
			const float localA = Luma(UpscaledTex.Load(int3(samplePixel, 0)).rgb);
			const float localB = Luma(GroundTruthTex.Load(int3(samplePixel, 0)).rgb);
			const float weight = Gaussian11[x + 5] * Gaussian11[y + 5];
			meanA += weight * localA;
			meanB += weight * localB;
			meanAA += weight * localA * localA;
			meanBB += weight * localB * localB;
			meanAB += weight * localA * localB;
		}
	}

	const float varianceA = max(meanAA - meanA * meanA, 0.0);
	const float varianceB = max(meanBB - meanB * meanB, 0.0);
	const float covariance = meanAB - meanA * meanB;
	const float c1 = 0.01 * 0.01;
	const float c2 = 0.03 * 0.03;
	const float numerator = (2.0 * meanA * meanB + c1) * (2.0 * covariance + c2);
	const float denominator = (meanA * meanA + meanB * meanB + c1) * (varianceA + varianceB + c2);
	const float localSsim = clamp(numerator / max(denominator, 1e-12), -1.0, 1.0);
	const float encodedSsim = localSsim * 0.5 + 0.5;

	Sums.InterlockedAdd(0, (uint)(difference * difference * FixedScale + 0.5));
	Sums.InterlockedAdd(4, (uint)(encodedSsim * FixedScale + 0.5));
}
