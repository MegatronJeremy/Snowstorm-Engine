// Spatial denoiser for the half-res RT GI (#125), compute stage. One edge-avoiding à-trous wavelet
// iteration over the half-res GI irradiance: for each pixel, gather a 5x5 neighbourhood at the current
// stride, weight each tap by the B3-spline wavelet kernel AND by depth/normal edge-stopping (from the
// full-res G-buffer guide), and write the weighted average. The caller runs this N times with a doubling
// stride (1,2,4,…) ping-ponging GITarget <-> scratch, so the blur widens each pass without growing the
// tap count — the à-trous ("with holes") trick. Depth+normal-only (no luminance/variance term): that's
// the spatial half of SVGF; the temporal half stays with TAA (#125 defers variance-guided moments).
//
// Reference: Dammertz et al. "Edge-Avoiding À-Trous Wavelet Transform for fast Global Illumination
// Filtering" (2010) — the spatial kernel SVGF/NRD build on. Edge-stopping weights reuse GIUpsample's
// depth/normal formulation (same G-buffer guide, same sigmas) so the denoiser and the upsample agree on
// what an edge is. Irradiance-only, like the trace: no albedo here (multiplied at full res post-upsample).
//
// This pass's inputs (GI SRV, guide SRV, output UAV, sampler, params) are set 0. No TLAS / bindless — a
// plain image filter, unlike GI.comp.hlsl.

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GIIn : register(t0, space0);          // half-res incoming irradiance (rgb) to filter
Texture2D<float4> GBufferNormal : register(t1, space0); // full-res guide: .xyz world normal, .w NDC depth
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u2, space0); // filtered half-res irradiance
SamplerState LinearSampler : register(s3, space0);      // guide sampling (GI is Load'd at integer texels)

cbuffer GIDenoiseCB : register(b4, space0)
{
	uint2 OutSize;      // half-res dispatch dimensions (== GI extent)
	int Step;           // à-trous tap stride in texels for this iteration (1,2,4,…)
	float KNormalPow;   // normal edge-stop exponent (higher = sharper crease rejection)

	float KDepthScale;  // depth edge-stop scale in NDC space (higher = tighter silhouette cut)
	float3 _Pad;
};

// B3-spline à-trous kernel row {1/16, 4/16, 6/16, 4/16, 1/16}; the 2D weight is the outer product.
static const float kKernel[5] = {0.0625, 0.25, 0.375, 0.25, 0.0625};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const int2 centerPx = int2(id.xy);
	// UV of this half-res pixel's center, used to sample the full-res guide (the guide is full-res, so its
	// depth/normal at this UV is the receiver at this half-res texel — same convention as GI.comp.hlsl).
	const float2 centerUV = (float2(centerPx) + 0.5) / float2(OutSize);
	const float4 gc = GBufferNormal.SampleLevel(LinearSampler, centerUV, 0);
	const float3 Nc = gc.xyz;
	const float Dc = gc.w;

	// Sky / no geometry (zero normal or far plane): pass the GI through untouched, matching the trace +
	// upsample guards. Filtering across a sky edge would bleed black into lit pixels.
	if (dot(Nc, Nc) < 1e-6 || Dc >= 1.0)
	{
		GIOut[centerPx] = GIIn.Load(int3(centerPx, 0));
		return;
	}

	float3 accum = float3(0, 0, 0);
	float wsum = 0.0;

	[unroll] for (int dy = -2; dy <= 2; ++dy)
	{
		[unroll] for (int dx = -2; dx <= 2; ++dx)
		{
			const int2 tap = centerPx + int2(dx, dy) * Step;
			const int2 tapC = clamp(tap, int2(0, 0), int2(OutSize) - 1);

			const float3 gi = GIIn.Load(int3(tapC, 0)).rgb;

			// Guide at the tap (full-res G-buffer at the tap's half-res texel-center UV).
			const float2 tapUV = (float2(tapC) + 0.5) / float2(OutSize);
			const float4 gt = GBufferNormal.SampleLevel(LinearSampler, tapUV, 0);

			// Edge-stopping: reject taps across normal creases / depth discontinuities. Sky taps (zero normal)
			// fall out via wN -> 0 (dot with a zero vector) and the far-plane depth diff, so no explicit guard.
			const float wN = pow(saturate(dot(Nc, gt.xyz)), KNormalPow);
			const float wD = exp(-abs(Dc - gt.w) * KDepthScale);
			const float wK = kKernel[dx + 2] * kKernel[dy + 2];
			const float w = wK * wN * wD;

			accum += gi * w;
			wsum += w;
		}
	}

	// wsum is always >= the center tap's weight (wN=wD=1 there), so it never underflows — no fallback needed.
	GIOut[centerPx] = float4(accum / wsum, 1.0);
}
