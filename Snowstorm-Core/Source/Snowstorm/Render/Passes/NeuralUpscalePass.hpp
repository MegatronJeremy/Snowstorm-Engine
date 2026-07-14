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

		// Record the inference into `ctx`: upsample `lowResColor` -> [temporal: warp prevHistory by velocity] ->
		// conv stack -> residual add, leaving the result in the pass's storage output (OutputView()). Call inside
		// a graph pass that declares lowResColor (and, when temporal, prevHistory + velocity) as Sampled reads,
		// AFTER a PrepareResources(...) that returned true this frame.
		//
		// Temporal path (#98): pass non-null `prevHistory` (the previous frame's full-res output) + `velocity`
		// (full-res motion vectors). The warp stage reprojects prevHistory into feature channels 3..5 and writes
		// the motion vector into 6..7, so the net sees an 8-channel input. `historyValid` = false on the first
		// temporal frame / after a resize (no usable history yet -> warp feeds zeros). Pass both null for the
		// SPATIAL path (#47) — unchanged 3-channel behavior. The two paths need models of matching input width
		// (SetTemporal selects which identity model is built); mixing a temporal model with a null-history call
		// (or vice versa) reads/leaves feature channels undefined.
		void Infer(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& lowResColor,
		           uint32_t outWidth, uint32_t outHeight, const Ref<TextureView>& prevHistory = nullptr,
		           const Ref<TextureView>& velocity = nullptr, bool historyValid = false);

		// Select the inference path BEFORE PrepareResources: true = temporal (8-ch input, warp stage, 8-ch
		// identity model); false = spatial (3-ch, #47). Switching rebuilds the model + feature buffers. The
		// trained-weights path (SetWeightsPath) overrides the identity model but the input width must still match
		// what the pass populates, so keep this in sync with how RenderSystem calls Infer.
		void SetTemporal(bool temporal);

		// The full-res storage texture Infer() writes for the given frame-in-flight slot — the one the neural
		// pass will write THIS frame. Pass the SAME frameIndex you'll pass to Infer, at graph-build time, so
		// tonemap is repointed at the exact texture this frame's Infer fills (each frame owns its own slot, so a
		// build-time OutputView() that returned "the last Infer's slot" would point at the OTHER in-flight
		// frame's output — a stale/garbage read). Sampled+Storage, RGBA16F. Null before resources are sized.
		[[nodiscard]] const Ref<TextureView>& OutputView(const uint32_t frameIndex) const
		{
			static const Ref<TextureView> kNull;
			return (frameIndex < m_OutputView.size()) ? m_OutputView[frameIndex] : kNull;
		}
		[[nodiscard]] bool HasOutput() const { return !m_OutputView.empty(); }

		// Point at a trained .ssnn (loaded lazily on the next Infer). Empty resets to the identity refiner.
		void SetWeightsPath(const std::string& path);

	private:
		void EnsurePipelines();
		void EnsureModel();
		void EnsureSizedResources(uint32_t w, uint32_t h);

		// Compute pipelines: input bilinear-upsample, the optional temporal history-warp, the generic per-layer
		// conv, the residual-add output.
		Ref<Pipeline> m_UpsamplePipeline;
		Ref<Pipeline> m_WarpPipeline;
		Ref<Pipeline> m_ConvPipeline;
		Ref<Pipeline> m_AddPipeline;
		Ref<Sampler> m_LinearClamp;

		// Temporal (#98) vs spatial (#47) inference. Selects the identity model's input width and whether Infer
		// runs the history-warp stage. Changing it rebuilds the model + feature buffers (via m_ModelDirty).
		bool m_Temporal = false;

		// The network + its GPU weight buffer (weights..bias per layer, contiguous) + per-layer float offsets.
		Neural::NeuralModel m_Model;
		Ref<Buffer> m_Weights;
		std::vector<size_t> m_LayerOffsets;
		std::string m_WeightsPath; // empty => identity refiner
		bool m_ModelDirty = true;

		// Feature maps as flat CHW float storage buffers (maxChannels*W*H each). Buffers (not texture arrays)
		// because that's the CPU reference's layout and the engine has no 2D-array texture views; chained conv
		// dispatches use a global compute-storage barrier between. Base holds the bilinear upsample (the
		// residual skip, kept live to the end), and A/B ping-pong the conv stack, plus the full-res storage
		// output texture. ALL per-frame-in-flight: two frames overlap on the GPU (BarrierComputeStorage only
		// orders within one command buffer), so a single shared set would let frame N+1's upsample clobber
		// frame N's data mid-flight — a nondeterministic race. One slot per in-flight frame, like MetricsPass.
		std::vector<Ref<Buffer>> m_FeatureBase;
		std::vector<Ref<Buffer>> m_FeatureA;
		std::vector<Ref<Buffer>> m_FeatureB;
		std::vector<Ref<Texture>> m_Output;
		std::vector<Ref<TextureView>> m_OutputView;
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
