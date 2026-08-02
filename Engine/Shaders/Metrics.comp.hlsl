// PSNR + windowed-SSIM reduction (#45, windowing #96). Reads the upscaled and ground-truth LDR present
// images (both full-res, same resolution — see CreatePresentTarget). One thread per pixel; values are the
// perceptual [0,1] tonemapped sRGB the viewer sees, converted to Rec.709 luma (the standard channel for
// grayscale PSNR/SSIM).
//
// Accumulated (over N pixels), in fixed-point via InterlockedAdd (HLSL has no float atomics):
//   [0] sum( (a-b)^2 )    -> MSE -> PSNR = 10*log10(1/MSE)   (genuinely global — correct for MSE)
//   [1] sum( SSIM_window ) -> MEAN SSIM = sum / N            (#96: windowed, not whole-image)
//
// #96: SSIM is now the Wang et al. MEAN SSIM — each thread computes SSIM over a local WxW window centered
// on its pixel (local means/variances/covariance) and accumulates that scalar; the CPU divides by the
// pixel count. This is the standard SSIM (the original paper's 11x11 Gaussian window; we use a cheaper
// uniform box window). The previous GLOBAL SSIM used one whole-image statistic — it can't see local
// structural error (a blurry region and a sharp region can cancel in the global variance), which is exactly
// what SSIM is meant to localize. Box-uniform (not Gaussian) keeps it one cheap pass; the window radius is
// small so the O(W^2) inner loop stays affordable at present resolution.

Texture2D<float4> UpscaledTex : register(t0, space0);   // tonemapped LDR, upscaled path
Texture2D<float4> GroundTruthTex : register(t1, space0); // tonemapped LDR, full-res ground truth
RWByteAddressBuffer Sums : register(u2, space0);         // uint fixed-point accumulators (see slot map above)

cbuffer MetricsCB : register(b3, space0)
{
	uint2 Resolution; // image size in pixels
	float FixedScale; // multiply a [0,1] contribution by this before rounding to uint (fixed-point)
	float _Pad;
};

// SSIM window radius: (2*R+1) square box. R=3 -> 7x7, a good structure/cost tradeoff vs the paper's 11x11.
#define SSIM_RADIUS 3

// SSIM stabilization constants (Wang et al.), for luma in [0,1] (L=1): C1=(0.01)^2, C2=(0.03)^2.
static const float kSsimC1 = 0.0001;
static const float kSsimC2 = 0.0009;

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

	// --- MSE term (per-pixel, global): for PSNR. ---
	const float a0 = Luma(UpscaledTex.Load(int3(p, 0)).rgb);
	const float b0 = Luma(GroundTruthTex.Load(int3(p, 0)).rgb);
	const float d = a0 - b0;
	Sums.InterlockedAdd(0, (uint)(d * d * FixedScale));

	// --- Windowed SSIM (#96): local moments over the (2R+1) box centered here, clamped at the image edge. ---
	float sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
	float count = 0.0;
	[unroll] for (int dy = -SSIM_RADIUS; dy <= SSIM_RADIUS; ++dy)
	{
		[unroll] for (int dx = -SSIM_RADIUS; dx <= SSIM_RADIUS; ++dx)
		{
			const int2 q = clamp(p + int2(dx, dy), int2(0, 0), int2(Resolution) - 1);
			const float a = Luma(UpscaledTex.Load(int3(q, 0)).rgb);
			const float b = Luma(GroundTruthTex.Load(int3(q, 0)).rgb);
			sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b;
			count += 1.0;
		}
	}

	const float inv = 1.0 / count;
	const float ma = sa * inv, mb = sb * inv;
	const float va = max(saa * inv - ma * ma, 0.0);
	const float vb = max(sbb * inv - mb * mb, 0.0);
	const float cab = sab * inv - ma * mb;

	const float num = (2.0 * ma * mb + kSsimC1) * (2.0 * cab + kSsimC2);
	const float den = (ma * ma + mb * mb + kSsimC1) * (va + vb + kSsimC2);
	const float ssim = saturate(num / den); // per-window SSIM in [0,1]

	Sums.InterlockedAdd(4, (uint)(ssim * FixedScale));
}
