#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;

	// PSNR + SSIM image-quality metrics (#45): a compute pass that reduces the upscaled and ground-truth LDR
	// present images into a handful of running sums (a GPU reduction, model on IBLBakePass), which the CPU
	// maps back to finalize PSNR (dB) and a global SSIM. Only added to the graph when render.metrics AND
	// render.compare are on (both images must exist). Owns the compute pipeline, a host-visible storage buffer
	// (written by the shader, mapped by the CPU — no separate readback copy), and a per-frame descriptor set.
	class MetricsPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Metrics"; }

		// The finalized result of the most recent Compute(). Valid == false until the first successful frame
		// (shader compiling, or a frame skipped). PSNR in dB (inf-capped); SSIM in [0,1].
		struct Result
		{
			bool Valid = false;
			float Psnr = 0.0f;
			float Ssim = 0.0f;
		};

		// Dispatch the reduction over `upscaled` vs `groundTruth` (both full-res LDR sample views of the same
		// size), reading last frame's buffer to finalize the metric. Because a host-visible buffer read the
		// same frame it was written would race the GPU, this reads the PREVIOUS frame's sums (1-frame lag,
		// like the GPU-pass timers) — fine for a metric that changes slowly along a scripted path. Records the
		// dispatch into `ctx`; returns immediately (no CPU stall). No-op until the shader has compiled.
		void Compute(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		             const Ref<TextureView>& upscaled, const Ref<TextureView>& groundTruth, uint32_t width, uint32_t height);

		[[nodiscard]] const Result& GetResult() const { return m_Result; }

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;

		// Per-frame-in-flight: the storage buffer of 6 uint accumulators (host-visible) + the descriptor set +
		// the params UBO. Double-buffered so the CPU finalizes frame N-1's sums while the GPU writes frame N.
		std::vector<Ref<Buffer>> m_SumBuffers;
		std::vector<Ref<Buffer>> m_ParamBuffers;
		std::vector<Ref<DescriptorSet>> m_Sets;
		std::vector<bool> m_Written; // this slot has been dispatched at least once (its sums are readable)

		Result m_Result;
	};
}
