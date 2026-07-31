// Depth+normal-aware (joint bilateral) upsample of the half-res AO factor to full res (#126). The scalar
// twin of GIUpsample.frag.hlsl: for each full-res pixel, gather the 2x2 half-res AO taps around it and
// weight each by (a) the bilinear footprint, (b) how close its guide depth is, and (c) how aligned its
// guide normal is — so AO does NOT bleed across depth discontinuities / normal creases. The guide is the
// full-res depth+normal G-buffer (.xyz normal, .w NDC depth). If every tap is rejected, fall back to the
// nearest tap. Same as GIUpsample but the source/accumulator is a scalar (.r) instead of rgb.
//
// Self-contained set-1 resources parked at high bindings (the shared fullscreen VS drags Engine.hlsli's
// set-1 material bindings). Paired with Fullscreen.vert.hlsl.

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky (#129 Inc 1b)

Texture2D<float4> AOTex : register(t4, space1);      // half-res AO factor in .r
Texture2D<float4> GBufferTex : register(t5, space1); // full-res guide: .xy oct normal, .z roughness, .w NDC depth
SamplerState LinearClamp : register(s6, space1);

cbuffer AOUpsampleCB : register(b7, space1)
{
	uint2 AOSize;   // half-res AO dimensions
	uint2 FullSize; // full-res output dimensions (unused directly; UV is resolution-independent)
};

struct FullscreenVSOut
{
	float4 PositionCS : SV_Position;
	float2 NDC : TEXCOORD0;
};

float main(FullscreenVSOut input) : SV_Target
{
	const float2 uv = float2(input.NDC.x * 0.5 + 0.5, input.NDC.y * 0.5 + 0.5);

	// This full-res pixel's guide (center). #129 Inc 1b: .xy is octahedral, decode it; depth in .w.
	const float4 gc = GBufferTex.SampleLevel(LinearClamp, uv, 0);
	const float3 Nc = DecodeNormalOct(gc.xy);
	const float Dc = gc.w;

	// Sky / no geometry (far-plane / cleared depth): fully open (AO = 1).
	if (IsSky(Dc))
	{
		return 1.0;
	}

	// Half-res texel footprint around uv: base texel + bilinear fraction.
	const float2 hf = uv * float2(AOSize) - 0.5;
	const float2 baseTexel = floor(hf);
	const float2 f = hf - baseTexel;

	const float bw[4] = {
	    (1.0 - f.x) * (1.0 - f.y),
	    f.x * (1.0 - f.y),
	    (1.0 - f.x) * f.y,
	    f.x * f.y,
	};
	const int2 off[4] = {int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1)};

	// Bilateral sigmas (match GIUpsample): sharp normal rejection across creases, NDC-space depth cut.
	const float kNormalPow = 8.0;
	const float kDepthScale = 2000.0;

	float accum = 0.0;
	float wsum = 0.0;
	float nearestAO = 1.0;
	float nearestW = -1.0;

	[unroll] for (int t = 0; t < 4; ++t)
	{
		const int2 tap = int2(baseTexel) + off[t];
		const int2 tapC = clamp(tap, int2(0, 0), int2(AOSize) - 1);
		const float ao = AOTex.Load(int3(tapC, 0)).r;

		const float2 tapUV = (float2(tapC) + 0.5) / float2(AOSize);
		const float4 gt = GBufferTex.SampleLevel(LinearClamp, tapUV, 0);

		const float wN = pow(saturate(dot(Nc, DecodeNormalOct(gt.xy))), kNormalPow);
		const float wD = exp(-abs(Dc - gt.w) * kDepthScale);
		const float w = bw[t] * wN * wD;

		accum += ao * w;
		wsum += w;

		if (bw[t] > nearestW)
		{
			nearestW = bw[t];
			nearestAO = ao;
		}
	}

	// All taps rejected (thin geometry) -> nearest, so we never divide by ~0.
	return (wsum > 1e-4) ? (accum / wsum) : nearestAO;
}
