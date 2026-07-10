// Generic 2D convolution layer for the neural upscaler (#47). One dispatch = one conv layer: reads an input
// feature map, applies a "same"-padded stride-1 KxK convolution + bias + optional ReLU, writes the output
// feature map. The network architecture is the SEQUENCE of these dispatches (chained by C++ in
// NeuralUpscalePass) with per-layer weight tensors — the ONNX/TensorRT model, not one giant unrolled shader.
//
// Feature maps are Texture2DArray<float4>: channel c lives at slice c/4, component c%4. So a C-channel map
// uses ceil(C/4) array slices. This packs channels into the GPU's native RGBA lanes (free storage-image
// writes + sampling) and caps this first refiner at C<=16 (4 slices). The CPU reference (NeuralMath.hpp)
// uses CHW-planar layout; the math is identical, only the channel addressing differs.
//
// Weights are [outC][inC][kH][kW] row-major (PyTorch layout), bias is [outC], both in one StructuredBuffer
// (bias appended after the weights). This matches Conv2DReference so exported weights map 1:1.

Texture2DArray<float4> InMap : register(t0, space0);
StructuredBuffer<float> Weights : register(t1, space0); // [outC*inC*k*k] then [outC] bias appended
[[vk::image_format("rgba16f")]] RWTexture2DArray<float4> OutMap : register(u2, space0);

cbuffer ConvCB : register(b3, space0)
{
	uint2 Size;      // feature-map W,H (input == output for same padding)
	uint InChannels; // C_in
	uint OutChannels; // C_out
	uint KernelSize; // K (1 or 3)
	uint Activation; // 0 = none, 1 = ReLU
	uint BiasOffset; // index into Weights where the [outC] bias block starts (== outC*inC*k*k)
	uint _Pad;
};

// Read channel c of the input map at pixel (x,y). Zero outside the image (same-padding border).
float ReadIn(int x, int y, uint c)
{
	if (x < 0 || y < 0 || x >= (int)Size.x || y >= (int)Size.y)
	{
		return 0.0f;
	}
	const uint slice = c >> 2;      // c / 4
	const uint comp = c & 3;        // c % 4
	return InMap.Load(int4(x, y, slice, 0))[comp];
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Size.x || id.y >= Size.y)
	{
		return;
	}

	const int x = (int)id.x;
	const int y = (int)id.y;
	const int kR = (int)(KernelSize / 2); // radius (K odd)

	// Accumulate all output channels for this pixel, writing one float4 per slice.
	const uint outSlices = (OutChannels + 3) / 4;
	for (uint s = 0; s < outSlices; ++s)
	{
		float4 outv = float4(0, 0, 0, 0);
		[unroll(4)]
		for (uint comp = 0; comp < 4; ++comp)
		{
			const uint oc = s * 4 + comp;
			if (oc >= OutChannels)
			{
				break;
			}

			float acc = Weights[BiasOffset + oc]; // bias
			for (uint ic = 0; ic < InChannels; ++ic)
			{
				for (uint ky = 0; ky < KernelSize; ++ky)
				{
					for (uint kx = 0; kx < KernelSize; ++kx)
					{
						const float iv = ReadIn(x + (int)kx - kR, y + (int)ky - kR, ic);
						const uint wIdx = ((oc * InChannels + ic) * KernelSize + ky) * KernelSize + kx;
						acc += iv * Weights[wIdx];
					}
				}
			}

			if (Activation == 1 && acc < 0.0f)
			{
				acc = 0.0f;
			}
			outv[comp] = acc;
		}
		OutMap[int3(x, y, (int)s)] = outv;
	}
}
