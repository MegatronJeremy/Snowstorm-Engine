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

	// Contrast-Adaptive Sharpening pass (#44): the display-space, hue-safe home for the sharpen that must
	// NOT live in the temporal resolve (a pre-tonemap linear-HDR sharpen turns overshoot into a hue shift
	// once ACES curves each channel). Runs AFTER tonemap on the tonemapped LDR image, exactly like FxaaPass
	// — same self-contained set-1 resources, same gamma-in / linear-out (sRGB HW re-encode on write, #79).
	// Only added to the graph when render.sharpen > 0 (RenderSystem gates it). Owns its pipeline + a
	// clamp-linear sampler + per-frame descriptor set/UBO. Structurally a twin of FxaaPass.
	class SharpenPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Sharpen"; }

		// Sharpen `srcSampleView` (the tonemapped LDR intermediate, a UNORM view) into the current render
		// target. `rcpFrame` = 1/viewport-size; `sharpness` in [0,1]. Records into `ctx`; no-op until the
		// shader has compiled. Uses the given frame index to pick the per-frame descriptor set/UBO.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const Ref<TextureView>& srcSampleView, const glm::vec2& rcpFrame, float sharpness, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler; // clamp-to-edge bilinear (created once)

		// Per-frame-in-flight descriptor set + its UBO (SharpenCB). Rebuilt lazily; the source view changes
		// on resize, so the set's texture binding is refreshed every Draw.
		std::vector<Ref<DescriptorSet>> m_Sets;
		std::vector<Ref<Buffer>> m_UniformBuffers;
	};
}
