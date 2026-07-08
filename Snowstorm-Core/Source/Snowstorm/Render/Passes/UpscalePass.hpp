#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// Internal-resolution upscale pass (#43): bilinear-resample the low-res HDR scene target into the
	// full-res HDR upscale target. The seam the neural super-resolution upscaler plugs into — it replaces
	// this pass's shader (same source/dest), the bilinear version is the baseline. Only added to the graph
	// when render.scale < 1 (RenderSystem gates it). Owns a clamp-linear sampler + a per-frame descriptor
	// set (set 1, texture+sampler); no UBO — UVs come from the fullscreen VS. Models on FxaaPass.
	class UpscalePass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Upscale"; }

		// Resample `srcSampleView` (the low-res HDR scene color) into the current (full-res HDR) target.
		// Records into `ctx`; no-op until the shader has compiled. Uses `frameIndex` for the per-frame set.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const Ref<TextureView>& srcSampleView, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		Ref<Sampler> m_Sampler; // clamp-to-edge bilinear

		std::vector<Ref<DescriptorSet>> m_Sets; // per frame-in-flight
	};
}
