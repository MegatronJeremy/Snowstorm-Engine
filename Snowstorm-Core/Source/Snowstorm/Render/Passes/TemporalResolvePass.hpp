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

	// Temporal resolve / TAA pass (#44): the payoff for jitter + motion vectors. Reprojects last frame's
	// resolved HDR (history) by the velocity buffer, neighborhood-clamps it against the current frame to
	// kill ghosting, blends, and writes the result — which feeds tonemap AND becomes next frame's history.
	// Classical TAA (the neural upscaler later replaces this shader at the same seam). Runs in HDR/linear
	// space, after forward(+upscale), before tonemap. Only added to the graph when render.aa == TAA
	// (RenderSystem gates it). Owns its pipeline + a clamp-linear sampler + per-frame descriptor set/UBO.
	// Resources model on FxaaPass (self-contained set 1, bindings parked high).
	class TemporalResolvePass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "TemporalResolve"; }

		// Blend current + reprojected history into the current render target (a full-res HDR history slot).
		// `current`/`history`/`velocity` are the three HDR/velocity source views; `rcpFrame` = 1/render-size;
		// `historyValid` false on the first frame / after a reset (outputs current only). Records into `ctx`;
		// no-op until the shader has compiled.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const Ref<TextureView>& current, const Ref<TextureView>& history, const Ref<TextureView>& velocity,
		          const glm::vec2& rcpFrame, bool historyValid, float blend, float maxBlend, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler; // clamp-to-edge bilinear (history reprojection tap)

		// Per-frame-in-flight descriptor set + its UBO (ResolveCB). The source views change each frame
		// (history ping-pongs, target resizes), so the texture bindings are refreshed every Draw.
		std::vector<Ref<DescriptorSet>> m_Sets;
		std::vector<Ref<Buffer>> m_UniformBuffers;
	};
}
