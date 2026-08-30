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
//
// PERMUTATION SS_DENOISE_HITDIST: the per-tap hit-distance edge-stop is AO-only (GI, reflections and both
// shadow chains pass HitDistPhi == 0). Its 25 per-tap loads are register-allocated for every consumer even
// though three of the four branch past them, and this shader is the frame's only occupancy-limited one, so
// the term is compiled out unless the variant asks for it. The DECLARATION and the single center Load stay
// unconditional: -fspv-preserve-bindings then keeps binding 3 in the reflected layout, so both variants
// share one descriptor-set layout and GIDenoisePass binds them identically. Defining the macro reproduces
// the pre-permutation shader exactly, so the AO variant is bit-identical to the old single build.

// ---- Set 0: this pass's own resources ----
Texture2D<float4> GIIn : register(t0, space0);          // half-res incoming irradiance (rgb) to filter
Texture2D<float4> GBufferNormal : register(t1, space0); // full-res guide: .xyz world normal, .w NDC depth
[[vk::image_format("rgba16f")]] RWTexture2D<float4> GIOut : register(u2, space0); // filtered half-res irradiance
// #130 Inc B: hit-distance guide (AO's normalized mean occluder distance in .a, same res + grid as GIIn) for
// the REBLUR-style edge-stop. Binding 3 was the #129 Inc 2c freed sampler slot — reusing it means no binding
// re-index. ALWAYS Load'd (below) so dxc can't strip it; GI/reflections bind their G-buffer here + pass
// HitDistPhi == 0 so the term is a no-op (their output stays bit-identical). AO binds the raw AO trace.
Texture2D<float4> HitDistGuide : register(t3, space0);
Texture2D<float> GBufferDepth : register(t5, space0); // fp32 NDC depth (D32 attachment), sampled directly

cbuffer GIDenoiseCB : register(b4, space0)
{
	uint2 OutSize;      // half-res dispatch dimensions (== GI extent)
	int Step;           // à-trous tap stride in texels for this iteration (1,2,4,…)
	float KNormalPow;   // normal edge-stop exponent (higher = sharper crease rejection)

	float KDepthScale;  // RELATIVE view-depth edge-stop scale (higher = tighter silhouette cut). Applied to a
	                    // LINEARIZED, relative depth diff — NOT raw NDC (which over-rejected near/grazing surfaces).
	float LumaPhi;      // SVGF luminance edge-stop scale (#129 Inc 3b); 0 => luminance term OFF (pre-3b behaviour)
	float HitDistPhi;   // #130 Inc B: hit-distance edge-stop scale (AO only); 0 => hit-distance term OFF (GI/refl)
	float Near;         // camera near/far to linearize the packed NDC depth for the edge-stop

	float Far;
	float PenumbraScale; // SIGMA-style penumbra kernel sizing (shadows only); 0 => identity (GI/AO/refl unchanged)
	float2 _Pad;
};

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

// B3-spline à-trous kernel row {1/16, 4/16, 6/16, 4/16, 1/16}; the 2D weight is the outer product.
static const float kKernel[5] = {0.0625, 0.25, 0.375, 0.25, 0.0625};

// B3-spline row weight by OFFSET (-2..2), computed rather than indexed. A dynamic index into the array
// above forces dxc to fully unroll (or spill it to scratch); this lets the outer loop stay rolled.
float B3Weight(int d)
{
	const int a = abs(d);
	return (a == 2) ? 0.0625 : ((a == 1) ? 0.25 : 0.375);
}

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
	const float Dc = GBufferDepth.Load(int3(centerTexel, 0)).r; // fp32 depth from the D32 attachment (was gc.w)
	const float linCenter = LinearizeViewDepth(Dc, Near, Far); // relative view-depth edge-stop (not raw NDC)

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
	// #130 Inc B: center hit distance, read by the AO edge-stop and the shadow penumbra sizing. Neither
	// variant compiles it in for GI/reflections, which consume neither.
#if defined(SS_DENOISE_HITDIST) || defined(SS_DENOISE_PENUMBRA)
	const float hitCenter = HitDistGuide.Load(int3(centerPx, 0)).a;
#endif

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

	// SIGMA-style penumbra-aware kernel sizing (shadows only; PenumbraScale == 0 => identity for GI/AO/reflections,
	// so their output stays bit-identical). Scale the à-trous tap stride by the receiver's occluder distance
	// (HitDistGuide.a, world units): a NEAR occluder (small) keeps a TIGHT kernel -> sharp contact shadow, a FAR
	// occluder WIDENS it -> smooth soft penumbra. This is the NRD SIGMA idea (penumbra ∝ occluder distance) that a
	// fixed-stride à-trous can't do: it otherwise over-blurs contacts or under-blurs soft penumbrae at one setting.
	float kStep = float(Step); // == Step exactly when PenumbraScale == 0 -> integer taps unchanged for other signals
#ifdef SS_DENOISE_PENUMBRA
	if (PenumbraScale > 0.0)
	{
		const float penumbra = saturate(hitCenter * PenumbraScale); // 0 at contact / fully-lit, ->1 for a distant occluder
		kStep = float(Step) * lerp(0.4, 2.2, penumbra);
	}
#endif

	// The OUTER loop stays rolled on purpose. Unrolling both let the compiler hoist ~60 of the 113 image
	// loads to the top of the shader and hold their results live, which cost 191 VGPRs and capped this at
	// 8/16 waves: it was the only occupancy-limited shader in the frame. Rolling the outer loop keeps one
	// row of taps in flight instead of the whole 5x5 and drops it to 72 VGPRs / 16 waves, no scratch.
	// The inner loop stays unrolled so dx stays a compile-time constant.
	//
	// This trades latency hiding for occupancy, and the trade is NOT uniformly favourable: measured on a
	// 9060 XT (ABBA-paired, 5 pairs), the full-res chains gain 36-47% and the wide-stride half-res
	// iteration gains 25%, but the STRIDE-1 iteration loses ~20%, where adjacent taps made the unrolled
	// batch cache-friendly enough that batching beat occupancy. Net -12.9% frame time, so it stays; the
	// per-stride refinement is a follow-up, not a reason to keep the 191-VGPR version.
	[loop] for (int dy = -2; dy <= 2; ++dy)
	{
		[unroll] for (int dx = -2; dx <= 2; ++dx)
		{
			const int2 tap = centerPx + int2(round(float2(dx, dy) * kStep));
			const int2 tapC = clamp(tap, int2(0, 0), int2(OutSize) - 1);

			const float4 tapIn = GIIn.Load(int3(tapC, 0));
			const float3 gi = tapIn.rgb;

			// Guide at the tap: POINT-fetch the full-res texel nearest the half-res tap center (never bilinear).
			const float2 tapUV = (float2(tapC) + 0.5) / float2(OutSize);
			const int2 tapTexel = clamp(int2(tapUV * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
			const float4 gt = GBufferNormal.Load(int3(tapTexel, 0));
			const float tapDepth = GBufferDepth.Load(int3(tapTexel, 0)).r; // fp32 depth (was gt.w)

			// Edge-stopping: reject taps across normal creases / depth discontinuities. A sky tap has depth ~1,
			// so its large depth diff (wD -> 0) drops it — no explicit sky guard needed on taps.
			const float wN = pow(saturate(dot(Nc, DecodeNormalOct(gt.xy))), KNormalPow);
			// RELATIVE view-depth edge-stop: |Δlinear| / centerLinear, so one KDepthScale works near AND far.
			// Raw NDC * fixed scale over-rejected near/grazing surfaces -> every tap dropped -> à-trous no-op.
			const float linTap = LinearizeViewDepth(tapDepth, Near, Far);
			const float dRel = abs(linCenter - linTap) / max(linCenter, 1e-4);
			const float wD = exp(-dRel * KDepthScale);
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
#ifdef SS_DENOISE_HITDIST
			if (HitDistPhi > 0.0)
			{
				const float hitTap = HitDistGuide.Load(int3(tapC, 0)).a;
				wH = exp(-abs(hitCenter - hitTap) * HitDistPhi);
			}
#endif
			const float wK = B3Weight(dx) * B3Weight(dy);
			const float w = wK * wN * wD * wL * wH;

			accum += gi * w;
			wsum += w;
			// Variance filters with the SQUARE of the weights (variance of a weighted sum), per SVGF.
			varAccum += (w * w) * max(tapIn.a, 0.0);
		}
	}

	// wsum is always >= the center tap's weight (wN=wD=wL=1 there), so it never underflows — no fallback needed.
	// Var[sum(w*c)/sum(w)] = sum(w^2*Var)/ (sum w)^2: the denominator is the SQUARE OF THE SUM, not the
	// sum of squares. Dividing by varWsum = sum(w^2) instead returns a weighted MEAN of the input
	// variances, which for N equal weights is N times too large and never decays across iterations, so
	// lumaDenom stays wide and the luminance edge-stop is inert after the first pass.
	const float outVar = (wsum > 1e-12) ? (varAccum / (wsum * wsum)) : centerVar;
	GIOut[centerPx] = float4(accum / wsum, outVar);
}
