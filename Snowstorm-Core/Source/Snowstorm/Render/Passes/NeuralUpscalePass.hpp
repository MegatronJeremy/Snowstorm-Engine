#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Neural/NeuralWeights.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;
	class Sampler;
	class DescriptorSet;

	// Neural super-resolution upscaler inference (#47). A compute path that replaces the bilinear UpscalePass:
	// bilinear-upsample the low-res scene color to full res, run a residual conv stack (the NeuralConv kernel
	// dispatched once per layer, ping-ponging two feature-map textures), and add the residual back. The
	// architecture IS the loaded NeuralModel's layer list — changing the net changes the model + weights, not
	// this pass. Structured like a minimal ML runtime: a few primitive kernels (conv/upsample/add) chained by
	// C++ with weights as an external buffer (cf. ONNX Runtime / TensorRT).
	//
	// Default weights are the code-defined identity refiner (zero output layer -> residual 0 -> output ==
	// bilinear), so the untrained pass is a provable no-op that validates the whole chain. A trained .ssnn set
	// via SetWeightsPath overrides it.
	class NeuralUpscalePass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "NeuralUpscale"; }

		// Build/resize the pipelines, model, and sized resources for a (outWidth,outHeight) inference. MUST be
		// called at graph-BUILD time (before AddPass), NOT inside the Execute lambda: a resize recreates the
		// bindless-registered output view and drains the GPU (WaitIdle), which is illegal mid-command-recording.
		// Returns true once everything is ready (shaders compiled + resources sized); Infer no-ops until then.
		bool PrepareResources(uint32_t outWidth, uint32_t outHeight);

		// Record the inference into `ctx`: upsample `lowResColor` -> conv stack -> residual add, leaving the
		// result in the pass's storage output (OutputView()). Call inside a graph pass that declares lowResColor
		// as a Sampled read, AFTER a PrepareResources(...) that returned true this frame.
		void Infer(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& lowResColor,
		           uint32_t outWidth, uint32_t outHeight);

		// The full-res storage texture holding the last Infer() result (null before the first successful run).
		// The caller repoints the tonemap's scene-color view at this. Sampled+Storage, RGBA16F.
		[[nodiscard]] const Ref<TextureView>& OutputView() const { return m_OutputView; }
		[[nodiscard]] bool HasOutput() const { return m_OutputView != nullptr; }

		// Point at a trained .ssnn (loaded lazily on the next Infer). Empty resets to the identity refiner.
		void SetWeightsPath(const std::string& path);

	private:
		void EnsurePipelines();
		void EnsureModel();
		void EnsureSizedResources(uint32_t w, uint32_t h);

		// Compute pipelines: input bilinear-upsample, the generic per-layer conv, the residual-add output.
		Ref<Pipeline> m_UpsamplePipeline;
		Ref<Pipeline> m_ConvPipeline;
		Ref<Pipeline> m_AddPipeline;
		Ref<Sampler> m_LinearClamp;

		// The network + its GPU weight buffer (weights..bias per layer, contiguous) + per-layer float offsets.
		Neural::NeuralModel m_Model;
		Ref<Buffer> m_Weights;
		std::vector<size_t> m_LayerOffsets;
		std::string m_WeightsPath; // empty => identity refiner
		bool m_ModelDirty = true;

		// Feature maps as flat CHW float storage buffers (maxChannels*W*H each). Buffers (not texture arrays)
		// because that's the CPU reference's layout and the engine has no 2D-array texture views; chained conv
		// dispatches use a global compute-storage barrier between. Three buffers: Base holds the bilinear
		// upsample (the residual skip connection, kept live to the end), and A/B ping-pong the conv stack. Plus
		// the full-res storage output texture.
		Ref<Buffer> m_FeatureBase;
		Ref<Buffer> m_FeatureA;
		Ref<Buffer> m_FeatureB;
		Ref<Texture> m_Output;
		Ref<TextureView> m_OutputView;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_MaxChannels = 0; // widest layer's channel count (sizes the feature buffers)

		// Transient per-dispatch descriptor sets / UBOs, ring-buffered by frame-in-flight index. Each Infer
		// allocates fresh sets+UBOs; they must stay alive until the GPU finishes that frame, so we keep one
		// generation per in-flight frame and only clear a slot when its frame comes around again (its prior
		// submission has retired). Clearing immediately would destroy descriptors the GPU is still reading.
		std::vector<std::vector<Ref<DescriptorSet>>> m_KeepAliveSets;
		std::vector<std::vector<Ref<Buffer>>> m_KeepAliveBuffers;
	};
}
