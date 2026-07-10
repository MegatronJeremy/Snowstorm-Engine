#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Snowstorm::Neural
{
	// Pure, GPU-free reference implementations of the neural-upscaler primitives (#47). These define the
	// EXACT math the compute shaders must reproduce — the shader is validated against this, and a Catch2 test
	// pins this against hand-computed cases. Everything here is host-side and header-only so it is trivially
	// unit-testable with no Vulkan/GPU dependency.
	//
	// Tensor layout convention (matches PyTorch, so exported weights map 1:1):
	//   - Feature maps are CHW: index (c, y, x) at c*H*W + y*W + x, row-major within each channel plane.
	//   - Conv weights are [outC][inC][kH][kW]: index oc*(inC*kH*kW) + ic*(kH*kW) + ky*kW + kx.
	//   - Bias is [outC].
	// Convolution is "same" padding (output H,W == input H,W), stride 1, zero padding at borders — the
	// standard configuration for a post-upsample SR refiner where every layer preserves spatial size.

	enum class Activation : uint32_t
	{
		None = 0,
		ReLU = 1,
	};

	inline float ApplyActivation(const float v, const Activation act)
	{
		switch (act)
		{
		case Activation::ReLU:
			return v > 0.0f ? v : 0.0f;
		case Activation::None:
			break;
		}
		return v;
	}

	// A CHW feature map with explicit dimensions. Data size must be Channels*Height*Width.
	struct FeatureMap
	{
		uint32_t Channels = 0;
		uint32_t Height = 0;
		uint32_t Width = 0;
		std::vector<float> Data; // CHW, size = Channels*Height*Width

		[[nodiscard]] float At(const uint32_t c, const uint32_t y, const uint32_t x) const
		{
			return Data[(static_cast<size_t>(c) * Height + y) * Width + x];
		}
		float& At(const uint32_t c, const uint32_t y, const uint32_t x)
		{
			return Data[(static_cast<size_t>(c) * Height + y) * Width + x];
		}
	};

	// One convolution layer's parameters. Weights is [outC][inC][kH][kW] flattened; Bias is [outC].
	struct ConvLayer
	{
		uint32_t InChannels = 0;
		uint32_t OutChannels = 0;
		uint32_t KernelSize = 3; // square kernel, odd (1 or 3)
		Activation Act = Activation::None;
		std::vector<float> Weights; // size = OutChannels*InChannels*KernelSize*KernelSize
		std::vector<float> Bias;    // size = OutChannels
	};

	// Reference "same"-padded, stride-1 2D convolution + bias + activation. Output has OutChannels channels and
	// the same H,W as the input. Border pixels see zero-padded neighbours (standard). This is the oracle the
	// NeuralConv.comp.hlsl shader must match bit-for-bit (within float tolerance).
	inline FeatureMap Conv2DReference(const FeatureMap& in, const ConvLayer& layer)
	{
		const uint32_t H = in.Height, W = in.Width;
		const uint32_t kR = layer.KernelSize / 2; // radius (kernel is odd)

		FeatureMap out;
		out.Channels = layer.OutChannels;
		out.Height = H;
		out.Width = W;
		out.Data.assign(static_cast<size_t>(layer.OutChannels) * H * W, 0.0f);

		for (uint32_t oc = 0; oc < layer.OutChannels; ++oc)
		{
			const float bias = oc < layer.Bias.size() ? layer.Bias[oc] : 0.0f;
			for (uint32_t y = 0; y < H; ++y)
			{
				for (uint32_t x = 0; x < W; ++x)
				{
					float acc = bias;
					for (uint32_t ic = 0; ic < layer.InChannels; ++ic)
					{
						for (uint32_t ky = 0; ky < layer.KernelSize; ++ky)
						{
							const int sy = static_cast<int>(y) + static_cast<int>(ky) - static_cast<int>(kR);
							if (sy < 0 || sy >= static_cast<int>(H))
							{
								continue; // zero padding
							}
							for (uint32_t kx = 0; kx < layer.KernelSize; ++kx)
							{
								const int sx = static_cast<int>(x) + static_cast<int>(kx) - static_cast<int>(kR);
								if (sx < 0 || sx >= static_cast<int>(W))
								{
									continue; // zero padding
								}
								const float iv = in.At(ic, static_cast<uint32_t>(sy), static_cast<uint32_t>(sx));
								const size_t wIdx = ((static_cast<size_t>(oc) * layer.InChannels + ic) * layer.KernelSize + ky) * layer.KernelSize + kx;
								acc += iv * layer.Weights[wIdx];
							}
						}
					}
					out.At(oc, y, x) = ApplyActivation(acc, layer.Act);
				}
			}
		}
		return out;
	}
}
