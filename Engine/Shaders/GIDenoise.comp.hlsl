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
// #129 Inc 2c: no sampler — both the GI input and the G-buffer guide are POINT-fetched via Load (never
// bilinear, to avoid cross-edge blend). Nothing in this pass samples, so no SamplerState is declared.

cbuffer GIDenoiseCB : register(b4, space0)
{
	uint2 OutSize;      // half-res dispatch dimensions (== GI extent)
	int Step;           // à-trous tap stride in texels for this iteration (1,2,4,…)
	float KNormalPow;   // normal edge-stop exponent (higher = sharper crease rejection)

	float KDepthScale;  // depth edge-stop scale in NDC space (higher = tighter silhouette cut)
	float LumaPhi;      // SVGF luminance edge-stop scale (#129 Inc 3b); 0 => luminance term OFF (pre-3b behaviour)
	float2 _Pad;
};

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

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
	// Guide at this half-res pixel: POINT-fetch the nearest full-res G-buffer texel (never bilinear — a linear
	// tap blends across silhouettes into a midpoint normal/depth, fuzzing the edge-stopping so the à-trous
	// smears GI across the edge, #129 Inc 2c). Map the half-res center UV to the full-res texel via GetDimensions.
	const float2 centerUV = (float2(centerPx) + 0.5) / float2(OutSize);
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 centerTexel = clamp(int2(centerUV * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float4 gc = GBufferNormal.Load(int3(centerTexel, 0));
	const float3 Nc = DecodeNormalOct(gc.xy); // #129 Inc 1b: .xy octahedral normal
	const float Dc = gc.w;

	// Sky / no geometry (far plane / cleared depth): pass the GI through untouched, matching the trace +
	// upsample guards. Filtering across a sky edge would bleed black into lit pixels.
	if (IsSky(Dc))
	{
		GIOut[centerPx] = GIIn.Load(int3(centerPx, 0));
		return;
	}

	const float3 centerGI = GIIn.Load(int3(centerPx, 0)).rgb;

	// SVGF luminance edge-stop (#129 Inc 3b): the fixed depth+normal kernel over-blurs converged detail and
	// under-blurs noisy regions equally. Adding a luminance term that ADAPTS to the local noise level widens
	// the effective filter where the signal is noisy (disocclusions / thin history) and keeps it tight where
	// it's already smooth. SVGF drives this from temporally-accumulated variance; we estimate variance
	// SPATIALLY from the 3x3 neighborhood of THIS pass's input instead — no moment buffers to plumb/desync,
	// ~the same result for a spatial à-trous (the temporal moments mainly help the very first disocclusion
	// frame, which our temporal stage + multiple à-trous passes already cover). LumaPhi == 0 => term OFF.
	const float lumaCenter = dot(centerGI, float3(0.2126, 0.7152, 0.0722));
	float lumaVar = 0.0;
	if (LumaPhi > 0.0)
	{
		float m1 = 0.0;
		float m2 = 0.0;
		[unroll] for (int vy = -1; vy <= 1; ++vy)
		{
			[unroll] for (int vx = -1; vx <= 1; ++vx)
			{
				const int2 vp = clamp(centerPx + int2(vx, vy), int2(0, 0), int2(OutSize) - 1);
				const float l = dot(GIIn.Load(int3(vp, 0)).rgb, float3(0.2126, 0.7152, 0.0722));
				m1 += l;
				m2 += l * l;
			}
		}
		const float mean = m1 / 9.0;
		lumaVar = max(m2 / 9.0 - mean * mean, 0.0);
	}
	// Denominator for the luminance weight: LumaPhi * stddev (+eps). High variance => large denom => the
	// luminance diff barely attenuates => WIDE blur; low variance => small denom => luminance differences cut
	// taps => detail preserved. This is the SVGF w_l = exp(-|l_c - l_t| / (phi*sqrt(var)+eps)) term.
	const float lumaDenom = LumaPhi * sqrt(lumaVar) + 1e-4;

	float3 accum = float3(0, 0, 0);
	float wsum = 0.0;

	[unroll] for (int dy = -2; dy <= 2; ++dy)
	{
		[unroll] for (int dx = -2; dx <= 2; ++dx)
		{
			const int2 tap = centerPx + int2(dx, dy) * Step;
			const int2 tapC = clamp(tap, int2(0, 0), int2(OutSize) - 1);

			const float3 gi = GIIn.Load(int3(tapC, 0)).rgb;

			// Guide at the tap: POINT-fetch the full-res texel nearest the half-res tap center (never bilinear).
			const float2 tapUV = (float2(tapC) + 0.5) / float2(OutSize);
			const int2 tapTexel = clamp(int2(tapUV * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
			const float4 gt = GBufferNormal.Load(int3(tapTexel, 0));

			// Edge-stopping: reject taps across normal creases / depth discontinuities. A sky tap has depth ~1,
			// so its large depth diff (wD -> 0) drops it — no explicit sky guard needed on taps.
			const float wN = pow(saturate(dot(Nc, DecodeNormalOct(gt.xy))), KNormalPow);
			const float wD = exp(-abs(Dc - gt.w) * KDepthScale);
			// Luminance term (#129 Inc 3b): variance-scaled, so noisy regions blur through it. 1.0 when off.
			float wL = 1.0;
			if (LumaPhi > 0.0)
			{
				const float lumaTap = dot(gi, float3(0.2126, 0.7152, 0.0722));
				wL = exp(-abs(lumaCenter - lumaTap) / lumaDenom);
			}
			const float wK = kKernel[dx + 2] * kKernel[dy + 2];
			const float w = wK * wN * wD * wL;

			accum += gi * w;
			wsum += w;
		}
	}

	// wsum is always >= the center tap's weight (wN=wD=wL=1 there), so it never underflows — no fallback needed.
	GIOut[centerPx] = float4(accum / wsum, 1.0);
}
