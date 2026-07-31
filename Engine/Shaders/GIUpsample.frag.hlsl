// Depth+normal-aware (joint bilateral) upsample of the half-res GI irradiance to full res (#124). For each
// full-res pixel, gather the 2x2 half-res GI taps around it and weight each by (a) the bilinear footprint,
// (b) how close its guide depth is to this pixel's, and (c) how aligned its guide normal is — so GI does
// NOT bleed across depth discontinuities / normal creases (the light-leak a plain bilinear upscale causes).
// The guide is the full-res depth+normal G-buffer (.xyz = world normal, .w = NDC depth), sampled at each
// tap's center. If every tap is rejected (thin geometry / all-different), fall back to the nearest tap.
//
// Reference: Metro Exodus / most deferred RTGI use exactly this joint-bilateral upsample after a reduced-res
// trace. Paired with Fullscreen.vert.hlsl. Self-contained resources on SET 1 (see UpscalePass note): the
// shared fullscreen VS drags Engine.hlsli's set-1 material bindings, so park these at high bindings.

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

Texture2D<float4> GITex : register(t4, space1);      // half-res incoming irradiance (rgb)
Texture2D<float4> GBufferTex : register(t5, space1); // full-res guide: .xy oct normal, .z roughness, .w NDC depth
SamplerState PointClamp : register(s6, space1); // #129 Inc 2c: NEAREST filter (GIUpsamplePass) — point-fetch the guide, never bilinear across edges

cbuffer GIUpsampleCB : register(b7, space1)
{
	uint2 GISize;   // half-res GI dimensions
	uint2 FullSize; // full-res output dimensions (unused directly; UV is resolution-independent)
};

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

float4 main(FullscreenVSOut input) : SV_Target
{
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);

	// This full-res pixel's guide (center). Sampled through a NEAREST (point) sampler — never bilinear: at a
	// silhouette a linear tap blends the two surfaces' normals/depths into a midpoint that matches NEITHER, so
	// the bilateral weights below reject wrongly and GI smears ~1px across the edge (#129 Inc 2c edge bleed).
	// The sampler is Filter::Nearest (GIUpsamplePass), so SampleLevel here is a point fetch. .xy oct normal, .w depth.
	const float4 gc = GBufferTex.SampleLevel(PointClamp, uv, 0);
	const float3 Nc = DecodeNormalOct(gc.xy);
	const float Dc = gc.w;

	// Sky / no geometry (the prepass clears depth to 1.0): no GI here.
	if (IsSky(Dc))
	{
		return float4(0, 0, 0, 1);
	}

	// Half-res texel footprint around uv: base texel + bilinear fraction.
	const float2 hf = uv * float2(GISize) - 0.5;
	const float2 baseTexel = floor(hf);
	const float2 f = hf - baseTexel;

	// Bilinear weights of the 2x2 taps.
	const float bw[4] = {
	    (1.0 - f.x) * (1.0 - f.y),
	    f.x * (1.0 - f.y),
	    (1.0 - f.x) * f.y,
	    f.x * f.y,
	};
	const int2 off[4] = {int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1)};

	// Bilateral sigmas. Normal: high power = sharp rejection across creases. Depth: NDC-space diff (jumps
	// hard at silhouettes), scaled so same-surface neighbours pass and edges are cut.
	const float kNormalPow = 8.0;
	const float kDepthScale = 2000.0;

	float3 accum = float3(0, 0, 0);
	float wsum = 0.0;
	float3 nearestGI = float3(0, 0, 0);
	float nearestW = -1.0;

	[unroll] for (int t = 0; t < 4; ++t)
	{
		const int2 tap = int2(baseTexel) + off[t];
		const int2 tapC = clamp(tap, int2(0, 0), int2(GISize) - 1);
		const float3 gi = GITex.Load(int3(tapC, 0)).rgb;

		// Guide at the tap's center: point-sampled (PointClamp) at the half-res tap-center UV — never bilinear,
		// same cross-edge-blend reason as the center guide above.
		const float2 tapUV = (float2(tapC) + 0.5) / float2(GISize);
		const float4 gt = GBufferTex.SampleLevel(PointClamp, tapUV, 0);

		const float wN = pow(saturate(dot(Nc, DecodeNormalOct(gt.xy))), kNormalPow);
		const float wD = exp(-abs(Dc - gt.w) * kDepthScale);
		const float w = bw[t] * wN * wD;

		accum += gi * w;
		wsum += w;

		// Track the highest bilinear-weight tap as the fallback (nearest-ish) if all are rejected.
		if (bw[t] > nearestW)
		{
			nearestW = bw[t];
			nearestGI = gi;
		}
	}

	// All taps rejected (thin geometry / a lone pixel between edges) -> nearest, so we never divide by ~0.
	const float3 result = (wsum > 1e-4) ? (accum / wsum) : nearestGI;
	return float4(result, 1.0);
}
