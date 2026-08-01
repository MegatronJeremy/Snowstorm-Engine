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
// #130 Inc B: hit-distance guide (AO's normalized mean occluder distance in .a, same res + grid as GIIn) for
// the REBLUR-style edge-stop. Binding 3 was the #129 Inc 2c freed sampler slot — reusing it means no binding
// re-index. ALWAYS Load'd (below) so dxc can't strip it; GI/reflections bind their G-buffer here + pass
// HitDistPhi == 0 so the term is a no-op (their output stays bit-identical). AO binds the raw AO trace.
Texture2D<float4> HitDistGuide : register(t3, space0);

cbuffer GIDenoiseCB : register(b4, space0)
{
	uint2 OutSize;      // half-res dispatch dimensions (== GI extent)
	int Step;           // à-trous tap stride in texels for this iteration (1,2,4,…)
	float KNormalPow;   // normal edge-stop exponent (higher = sharper crease rejection)

	float KDepthScale;  // depth edge-stop scale in NDC space (higher = tighter silhouette cut)
	float LumaPhi;      // SVGF luminance edge-stop scale (#129 Inc 3b); 0 => luminance term OFF (pre-3b behaviour)
	float HitDistPhi;   // #130 Inc B: hit-distance edge-stop scale (AO only); 0 => hit-distance term OFF (GI/refl)
	float _Pad;
};

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

// B3-spline à-trous kernel row {1/16, 4/16, 6/16, 4/16, 1/16}; the 2D weight is the outer product.
static const float kKernel[5] = {0.0625, 0.25, 0.375, 0.25, 0.0625};

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

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

	const float4 centerIn = GIIn.Load(int3(centerPx, 0));
	const float3 centerGI = centerIn.rgb;
	const float centerVar = max(centerIn.a, 0.0); // #129 Inc 3c: SVGF variance rides .a (from the temporal pass)
	const float lumaCenter = Luma(centerGI);
	// #130 Inc B: center hit distance for the REBLUR-style edge-stop (AO only; HitDistPhi == 0 => unused).
	const float hitCenter = HitDistGuide.Load(int3(centerPx, 0)).a;

	// SVGF luminance edge-stop (#129 Inc 3c, textbook temporal-moment variant). The denominator uses the
	// temporally-accumulated variance (not a per-pass spatial estimate as in Inc 3b), pre-blurred by a 3x3
	// gaussian so a single noisy variance texel doesn't destabilize the weight (the SVGF g_{3x3}(Var) step).
	// High variance (noisy / just-disoccluded) => large denom => luminance differences barely attenuate =>
	// WIDE blur; low variance (converged) => small denom => luminance edges preserved. LumaPhi == 0 => OFF.
	float varBlur = centerVar;
	if (LumaPhi > 0.0)
	{
		float vs = 0.0;
		float vw = 0.0;
		[unroll] for (int vy = -1; vy <= 1; ++vy)
		{
			[unroll] for (int vx = -1; vx <= 1; ++vx)
			{
				const int2 vp = clamp(centerPx + int2(vx, vy), int2(0, 0), int2(OutSize) - 1);
				const float gw = kKernel[vx + 2] * kKernel[vy + 2]; // reuse the B3-spline row (index 1..3) as a 3x3 gaussian
				vs += max(GIIn.Load(int3(vp, 0)).a, 0.0) * gw;
				vw += gw;
			}
		}
		varBlur = vs / max(vw, 1e-8);
	}
	const float lumaDenom = LumaPhi * sqrt(max(varBlur, 0.0)) + 1e-4;

	float3 accum = float3(0, 0, 0);
	float wsum = 0.0;
	float varAccum = 0.0; // #129 Inc 3c part 2: filter the variance through the à-trous with SQUARED weights
	float varWsum = 0.0;

	[unroll] for (int dy = -2; dy <= 2; ++dy)
	{
		[unroll] for (int dx = -2; dx <= 2; ++dx)
		{
			const int2 tap = centerPx + int2(dx, dy) * Step;
			const int2 tapC = clamp(tap, int2(0, 0), int2(OutSize) - 1);

			const float4 tapIn = GIIn.Load(int3(tapC, 0));
			const float3 gi = tapIn.rgb;

			// Guide at the tap: POINT-fetch the full-res texel nearest the half-res tap center (never bilinear).
			const float2 tapUV = (float2(tapC) + 0.5) / float2(OutSize);
			const int2 tapTexel = clamp(int2(tapUV * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
			const float4 gt = GBufferNormal.Load(int3(tapTexel, 0));

			// Edge-stopping: reject taps across normal creases / depth discontinuities. A sky tap has depth ~1,
			// so its large depth diff (wD -> 0) drops it — no explicit sky guard needed on taps.
			const float wN = pow(saturate(dot(Nc, DecodeNormalOct(gt.xy))), KNormalPow);
			const float wD = exp(-abs(Dc - gt.w) * KDepthScale);
			// SVGF luminance term: variance-scaled, so noisy regions blur through it. 1.0 when off.
			float wL = 1.0;
			if (LumaPhi > 0.0)
			{
				wL = exp(-abs(lumaCenter - Luma(gi)) / lumaDenom);
			}
			// #130 Inc B: hit-distance term (REBLUR-style). Rejects taps whose occluder distance differs from
			// the center's, so a near contact-shadow gradient (small hit distance) isn't blurred into distant
			// AO. AO-only: GI/reflections pass HitDistPhi == 0 => wH == 1 => identical output. Always Load the
			// guide (above) so the binding is never stripped even when the term is off.
			float wH = 1.0;
			if (HitDistPhi > 0.0)
			{
				const float hitTap = HitDistGuide.Load(int3(tapC, 0)).a;
				wH = exp(-abs(hitCenter - hitTap) * HitDistPhi);
			}
			const float wK = kKernel[dx + 2] * kKernel[dy + 2];
			const float w = wK * wN * wD * wL * wH;

			accum += gi * w;
			wsum += w;
			// Variance filters with the SQUARE of the weights (variance of a weighted sum), per SVGF.
			varAccum += (w * w) * max(tapIn.a, 0.0);
			varWsum += w * w;
		}
	}

	// wsum is always >= the center tap's weight (wN=wD=wL=1 there), so it never underflows — no fallback needed.
	const float outVar = (varWsum > 1e-12) ? (varAccum / varWsum) : centerVar;
	GIOut[centerPx] = float4(accum / wsum, outVar);
}
