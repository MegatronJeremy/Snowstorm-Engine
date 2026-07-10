// PSNR + SSIM reduction (#45). Reads the upscaled and ground-truth LDR present images (both full-res, the
// same resolution — see CreatePresentTarget) and accumulates the running sums a full-image quality compare
// needs, into a 6-slot buffer the CPU maps back to finalize the numbers. One thread per pixel; each atomically
// adds its contribution. Values are perceptual [0,1] (the tonemapped sRGB result the viewer sees) converted
// to a scalar luma, which is the standard channel for grayscale PSNR/SSIM.
//
// Accumulated (over N pixels), in fixed-point via InterlockedAdd (HLSL has no float atomics):
//   [0] sum( (a-b)^2 )   -> MSE -> PSNR = 10*log10(1/MSE)
//   [1] sum(a)  [2] sum(b)  [3] sum(a^2)  [4] sum(b^2)  [5] sum(a*b)   -> GLOBAL SSIM
// SSIM here uses whole-image (global) statistics, not a per-pixel sliding window — a common, defensible
// simplification for a benchmark metric; it captures luminance/contrast/structure agreement over the frame.

Texture2D<float4> UpscaledTex : register(t0, space0);   // tonemapped LDR, upscaled path
Texture2D<float4> GroundTruthTex : register(t1, space0); // tonemapped LDR, full-res ground truth
RWByteAddressBuffer Sums : register(u2, space0);         // 6 x uint fixed-point accumulators

cbuffer MetricsCB : register(b3, space0)
{
	uint2 Resolution; // image size in pixels
	float FixedScale; // multiply a [0,1] contribution by this before rounding to uint (fixed-point)
	float _Pad;
};

float Luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722)); // Rec.709
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Resolution.x || id.y >= Resolution.y)
	{
		return;
	}

	const int2 p = int2(id.xy);
	const float a = Luma(UpscaledTex.Load(int3(p, 0)).rgb);   // in [0,1]
	const float b = Luma(GroundTruthTex.Load(int3(p, 0)).rgb);
	const float d = a - b;

	// Fixed-point: each per-pixel term is in [0,1], so scaling by FixedScale and summing over up to a few
	// million pixels stays well within uint range (host picks FixedScale from the pixel count).
	Sums.InterlockedAdd(0, (uint)(d * d * FixedScale));
	Sums.InterlockedAdd(4, (uint)(a * FixedScale));
	Sums.InterlockedAdd(8, (uint)(b * FixedScale));
	Sums.InterlockedAdd(12, (uint)(a * a * FixedScale));
	Sums.InterlockedAdd(16, (uint)(b * b * FixedScale));
	Sums.InterlockedAdd(20, (uint)(a * b * FixedScale));
}
