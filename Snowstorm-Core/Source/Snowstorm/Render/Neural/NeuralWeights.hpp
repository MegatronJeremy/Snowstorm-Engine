#pragma once

#include "Snowstorm/Render/Neural/NeuralMath.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm::Neural
{
	// A trained neural-upscaler network as a sequence of conv layers (#47). This is the whole model the
	// NeuralUpscalePass runs: bilinear-upsample the low-res input, run these layers as a residual stack, add
	// the residual back. The architecture IS this layer list (+ the per-layer weight tensors) — changing the
	// net means changing this list and the .ssnn file, not the compute shader.
	//
	// The .ssnn on-disk format is the training pipeline's export target and the engine's load target:
	//   char[4]  magic  = "SSNN"
	//   uint32   version = 1
	//   uint32   layerCount
	//   per layer: uint32 inC, outC, kernelSize, activation(0 none / 1 relu),
	//              float[outC*inC*k*k] weights ([outC][inC][kH][kW]),
	//              float[outC] bias
	// Little-endian, tightly packed. Matches Conv2DReference's tensor layout so PyTorch state_dicts export 1:1.
	struct NeuralModel
	{
		std::vector<ConvLayer> Layers;

		// Total float count across all layers' weights + biases (the size of the packed GPU weight buffer).
		[[nodiscard]] size_t TotalFloats() const;

		// Flatten every layer's [weights..., bias...] blocks, in layer order, into one contiguous float array —
		// the exact buffer uploaded to the GPU (each layer's ConvCB.BiasOffset indexes into its own sub-block).
		[[nodiscard]] std::vector<float> PackWeights() const;

		// Per-layer float offset into PackWeights()'s array where layer i's block begins.
		[[nodiscard]] std::vector<size_t> LayerFloatOffsets() const;
	};

	// Build the default residual refiner architecture: bilinear input (3ch) -> conv 3->16 (ReLU) -> conv
	// 16->16 (ReLU) -> conv 16->3 (none), all 3x3. The FINAL layer's weights AND bias are all zero, so the
	// residual is exactly 0 and the network output == the bilinear input — a provable no-op used to validate
	// the whole GPU conv chain before any training exists. Real training overwrites these weights.
	NeuralModel MakeIdentityRefiner();

	// Serialize / parse the .ssnn format. SaveModel returns false on I/O error; LoadModel returns false on a
	// missing file, bad magic, or truncated/inconsistent data (and leaves `out` unspecified).
	bool SaveModel(const std::string& path, const NeuralModel& model);
	bool LoadModel(const std::string& path, NeuralModel& out);
}
