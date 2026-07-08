#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;

	// FXAA anti-aliasing pass (#40/#82): the spatial-AA baseline for the neural-upscaler comparison. Runs
	// after tonemap, sampling the tonemapped LDR image (bilinear, in gamma space) and edge-blending into
	// the present target. Unlike the tonemap pass it OWNS a descriptor set (set 0): a combined image +
	// sampler for the source + a small UBO with the inverse resolution. Only added to the graph when
	// render.aa != 0 (RenderSystem gates it). Owns its pipeline, rebuilt when the target format changes.
	class FxaaPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "FXAA"; }

		// Blend `srcSampleView` (the tonemapped LDR intermediate, a UNORM view) into the current render
		// target. `rcpFrame` = 1/viewport-size (texel step for the taps). Records into `ctx`; no-op until
		// the shader has compiled. Uses the given frame index to pick the per-frame descriptor set/UBO.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const Ref<TextureView>& srcSampleView, const glm::vec2& rcpFrame, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler; // clamp-to-edge bilinear (created once)

		// Per-frame-in-flight descriptor set + its UBO (RcpFrame). Rebuilt lazily; the source texture view
		// changes on resize, so the set's texture binding is refreshed every Draw.
		std::vector<Ref<DescriptorSet>> m_Sets;
		std::vector<Ref<Buffer>> m_UniformBuffers;
	};
}
