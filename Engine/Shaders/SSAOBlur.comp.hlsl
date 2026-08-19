// Depth+normal-aware bilateral blur of the half-res SSAO factor (#151). The SSAO denoiser — deliberately a
// plain spatial bilateral blur, NOT the SVGF temporal+à-trous chain the RT AO path uses (SSAO is already
// temporally stable, so it needs no history reproject). Removes the frame-static kernel-rotation noise
// SSAO.comp leaves while respecting silhouettes/creases (edge-stopping on the full-res G-buffer normal +
// depth, exactly like AOUpsample.frag). Runs at the half-res AO grid: AOTarget -> AOBlurTarget. The result
// feeds the existing depth+normal bilateral upsample to full res, so both AO techniques share that tail.

#include "Include/GBufferEncode.hlsli" // oct-normal decode + IsSky + LinearizeViewDepth

static const int KERNEL_RADIUS = 2; // 5x5 half-res taps — matches the SSAO rotation frequency

Texture2D<float4> AOIn : register(t0, space0);          // half-res SSAO factor in .r
Texture2D<float4> GBufferNormal : register(t1, space0); // full-res guide: .xy oct normal, .z roughness
Texture2D<float> GBufferDepth : register(t2, space0);   // fp32 NDC depth (D32 attachment)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> AOOut : register(u3, space0); // blurred half-res factor

cbuffer SSAOBlurCB : register(b4, space0)
{
	uint2 OutSize;    // half-res dimensions (AO grid)
	float Near;       // camera near/far to linearize NDC depth for the edge-stop
	float Far;
	float DepthSigma; // relative view-depth edge-stop tightness (render.rt.depthsigma)
	float _Pad0;
	float _Pad1;
	float _Pad2;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(OutSize);

	// Center guide (point-fetched at the full-res texel nearest this half-res pixel). Sky -> fully open.
	uint2 gbDims;
	GBufferNormal.GetDimensions(gbDims.x, gbDims.y);
	const int2 gbTexel = clamp(int2(uv * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
	const float centerDepth = GBufferDepth.Load(int3(gbTexel, 0)).r;
	if (IsSky(centerDepth))
	{
		AOOut[id.xy] = 1.0;
		return;
	}
	const float3 Nc = DecodeNormalOct(GBufferNormal.Load(int3(gbTexel, 0)).xy);
	const float linCenter = LinearizeViewDepth(centerDepth, Near, Far);

	const float kNormalPow = 8.0; // sharp rejection across creases (matches AOUpsample)

	float accum = 0.0;
	float wsum = 0.0;
	[unroll] for (int dy = -KERNEL_RADIUS; dy <= KERNEL_RADIUS; ++dy)
	{
		[unroll] for (int dx = -KERNEL_RADIUS; dx <= KERNEL_RADIUS; ++dx)
		{
			const int2 tap = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(OutSize) - 1);
			const float ao = AOIn.Load(int3(tap, 0)).r;

			// Guide at the tap's half-res center, point-fetched from the full-res G-buffer.
			const float2 tapUV = (float2(tap) + 0.5) / float2(OutSize);
			const int2 tapTexel = clamp(int2(tapUV * float2(gbDims)), int2(0, 0), int2(gbDims) - 1);
			const float tapDepth = GBufferDepth.Load(int3(tapTexel, 0)).r;
			if (IsSky(tapDepth))
			{
				continue;
			}
			const float3 Nt = DecodeNormalOct(GBufferNormal.Load(int3(tapTexel, 0)).xy);

			const float wN = pow(saturate(dot(Nc, Nt)), kNormalPow);
			const float linTap = LinearizeViewDepth(tapDepth, Near, Far);
			const float dRel = abs(linCenter - linTap) / max(linCenter, 1e-4);
			const float wD = exp(-dRel * DepthSigma);
			const float w = wN * wD;

			accum += ao * w;
			wsum += w;
		}
	}

	// Every neighbour rejected (a thin feature) -> keep this pixel's own value rather than divide by ~0.
	AOOut[id.xy] = (wsum > 1e-4) ? (accum / wsum) : AOIn.Load(int3(int2(id.xy), 0)).r;
}
