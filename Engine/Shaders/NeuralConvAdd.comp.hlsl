// Neural upscaler tail (#101): the final conv layer fused with the residual add. Computes
// output = BaseMap(ch 0..2) + conv(InMap) for a 3-channel last layer and writes the storage image the tonemap
// reads, so the residual feature map is never written to or read back from global memory (one dispatch, one
// barrier and one descriptor set fewer than NeuralConv + NeuralResidualAdd). Same weight layout and
// ConvCB as NeuralConv.comp.hlsl; the naive per-pixel loop is used at both precisions since the tail is only
// 3 output channels (no accumulator array to spill).

#ifdef SS_FP16
StructuredBuffer<float16_t> Weights : register(t1, space0);
#else
StructuredBuffer<float> Weights : register(t1, space0);
#endif
StructuredBuffer<float> InMap : register(t0, space0);   // CHW, InChannels*H*W floats
StructuredBuffer<float> BaseMap : register(t4, space0); // bilinear-upsampled base (CHW, channels 0..2)
[[vk::image_format("rgba16f")]] RWTexture2D<float4> OutImage : register(u2, space0);

cbuffer ConvCB : register(b3, space0)
{
	uint2 Size;
	uint InChannels;
	uint OutChannels; // always 3
	uint KernelSize;  // 1 or 3
	uint Activation;  // 0 none, 1 ReLU
	uint WeightOffset;
	uint BiasOffset;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Size.x || id.y >= Size.y)
	{
		return;
	}

	const int x = (int)id.x;
	const int y = (int)id.y;
	const int kR = (int)(KernelSize / 2);
	const uint plane = Size.x * Size.y;
	const uint pix = id.y * Size.x + id.x;

	float3 res;
	[unroll] for (uint oc = 0; oc < 3; ++oc)
	{
		float acc = (float)Weights[BiasOffset + oc];
		for (uint ic = 0; ic < InChannels; ++ic)
		{
			for (uint ky = 0; ky < KernelSize; ++ky)
			{
				const int sy = y + (int)ky - kR;
				for (uint kx = 0; kx < KernelSize; ++kx)
				{
					const int sx = x + (int)kx - kR;
					if (sx < 0 || sy < 0 || sx >= (int)Size.x || sy >= (int)Size.y)
					{
						continue;
					}
					const uint wIdx = WeightOffset + ((oc * InChannels + ic) * KernelSize + ky) * KernelSize + kx;
					acc += InMap[ic * plane + (uint)sy * Size.x + (uint)sx] * (float)Weights[wIdx];
				}
			}
		}
		if (Activation == 1 && acc < 0.0f)
		{
			acc = 0.0f;
		}
		res[oc] = acc;
	}

	const float3 base = float3(BaseMap[pix], BaseMap[plane + pix], BaseMap[2 * plane + pix]);
	OutImage[id.xy] = float4(base + res, 1.0f);
}
