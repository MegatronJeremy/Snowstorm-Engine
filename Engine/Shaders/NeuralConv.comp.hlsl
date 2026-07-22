// Generic 2D convolution layer for the neural upscaler (#47). One dispatch = one conv layer: reads an input
// feature map, applies a "same"-padded stride-1 KxK convolution + bias + optional ReLU, writes the output
// feature map. The network architecture is the SEQUENCE of these dispatches (chained by C++ in
// NeuralUpscalePass) with per-layer weight tensors — the ONNX/TensorRT model, not one giant unrolled shader.
//
// Feature maps are flat CHW float buffers (channel c, row y, col x at c*H*W + y*W + x) — exactly the layout
// of the NeuralMath.hpp CPU reference, so the shader is validated 1:1 against it. This matches how ML
// runtimes hold tensors (flat NCHW) and needs only memory barriers between layers (no image-layout dance).
//
// Weights are [outC][inC][kH][kW] row-major (PyTorch), bias [outC], both in one buffer with the bias block
// appended after the weights (BiasOffset). One thread per (output pixel), looping all output channels.

StructuredBuffer<float> InMap : register(t0, space0);   // CHW, InChannels*H*W floats
StructuredBuffer<float> Weights : register(t1, space0); // [outC*inC*k*k] then [outC] bias at BiasOffset
RWStructuredBuffer<float> OutMap : register(u2, space0); // CHW, OutChannels*H*W floats

cbuffer ConvCB : register(b3, space0)
{
	uint2 Size;      // feature-map W,H (input == output, same padding)
	uint InChannels;
	uint OutChannels;
	uint KernelSize;  // 1 or 3
	uint Activation;  // 0 none, 1 ReLU
	uint WeightOffset; // global float index where this layer's weights begin
	uint BiasOffset;   // global float index where this layer's bias begins
};

// Read input channel c at (x,y); zero outside the image (same-padding border).
float ReadIn(int x, int y, uint c)
{
	if (x < 0 || y < 0 || x >= (int)Size.x || y >= (int)Size.y)
	{
		return 0.0f;
	}
	return InMap[(c * Size.y + (uint)y) * Size.x + (uint)x];
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
	const int kR = (int)(KernelSize / 2);

	for (uint oc = 0; oc < OutChannels; ++oc)
	{
		float acc = Weights[BiasOffset + oc]; // bias
		for (uint ic = 0; ic < InChannels; ++ic)
		{
			for (uint ky = 0; ky < KernelSize; ++ky)
			{
				for (uint kx = 0; kx < KernelSize; ++kx)
				{
					const float iv = ReadIn(x + (int)kx - kR, y + (int)ky - kR, ic);
					const uint wIdx = WeightOffset + ((oc * InChannels + ic) * KernelSize + ky) * KernelSize + kx;
					acc += iv * Weights[wIdx];
				}
			}
		}
		if (Activation == 1 && acc < 0.0f)
		{
			acc = 0.0f;
		}
		OutMap[(oc * Size.y + (uint)y) * Size.x + (uint)x] = acc;
	}
}
