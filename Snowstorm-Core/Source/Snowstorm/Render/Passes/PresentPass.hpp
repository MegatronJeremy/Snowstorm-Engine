#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// Runtime present pass (#4): copies the primary viewport's final LDR image (its PresentSampleView) onto
	// the swapchain with a fullscreen triangle. The Runtime has no ImGui backend, so the editor's ImGui
	// swapchain pass never runs and the swapchain is composed by nothing — this is the non-ImGui equivalent.
	// Structurally a slimmed SharpenPass/FxaaPass: same self-contained set-1 (t4 texture + s5 sampler),
	// Fullscreen.vert + Present.frag, but a pure copy (no cbuffer / params). NO color conversion — the
	// swapchain is UNORM and the source is a UNORM sample view, so bytes pass straight through (same as the
	// ImGui blit). Gated in by RenderSystem only when there's no ImGui backend.
	class PresentPass final
	{
	public:
		// Copy `srcSampleView` (the viewport's PresentSampleView, a UNORM view) into the current render
		// target (the swapchain). `colorFormat` = the swapchain color format the pipeline builds for.
		// Records into `ctx`; no-op until the shader has compiled. `frameIndex` picks the per-frame set.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const Ref<TextureView>& srcSampleView, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler; // clamp-to-edge bilinear (created once)

		// Per-frame-in-flight descriptor set. No UBO (pure copy). The source view changes on resize, so the
		// texture binding is refreshed every Draw.
		std::vector<Ref<DescriptorSet>> m_Sets;
	};
}
