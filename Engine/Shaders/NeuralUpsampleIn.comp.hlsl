// Neural upscaler input stage (#47): bilinear-upsample the low-res HDR scene color to full resolution and
// write it as the first feature map (channels 0..2 = R,G,B in a flat CHW buffer). This is the network's
// input AND the residual base — NeuralResidualAdd sums the conv-stack residual back onto this same buffer,
// so a zero-residual (identity) network outputs exactly this bilinear result.

Texture2D<float4> LRColor : register(t0, space0);
SamplerState LinearClamp : register(s1, space0);
RWStructuredBuffer<float> OutMap : register(u2, space0); // CHW, >=3 channels * H * W floats

cbuffer UpsampleCB : register(b3, space0)
{
	uint2 OutSize; // full-res feature-map W,H
	uint2 _Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= OutSize.x || id.y >= OutSize.y)
	{
		return;
	}
	// Sample the low-res source at the output pixel center (bilinear via linear-clamp) — the same resample the
	// bilinear UpscalePass does, so the identity network is bit-comparable to it.
	const float2 uv = (float2(id.xy) + 0.5f) / float2(OutSize);
	const float3 rgb = LRColor.SampleLevel(LinearClamp, uv, 0).rgb;

	const uint plane = OutSize.x * OutSize.y;
	const uint pix = id.y * OutSize.x + id.x;
	OutMap[0 * plane + pix] = rgb.r; // channel 0
	OutMap[1 * plane + pix] = rgb.g; // channel 1
	OutMap[2 * plane + pix] = rgb.b; // channel 2
}
