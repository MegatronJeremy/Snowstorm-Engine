#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	class RendererService;
	class CommandContext;
	class Pipeline;

	// Tonemap/output post-process pass (#53). Reads the linear-HDR scene color (sampled from the scene RT
	// via a bindless index) and writes exposure -> ACES filmic -> sRGB into the LDR present target with a
	// fullscreen triangle. This is the single place the output transform lives — the mesh/sky shaders now
	// emit raw linear radiance. Owns its pipeline (rebuilt only when the present target's color format
	// changes). The seam a future upscaler / AA plugs into.
	class PostProcessPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "PostProcess"; }

		// Tonemap the HDR scene color into the current (LDR) render target. `params` carries the HDR scene
		// color bindless slot + the motion-vector debug fields (#44); `colorFormat` is the present target's
		// format (drives the lazy pipeline build). Records into `ctx` (the graph's per-frame command
		// context). No-op until the shader has compiled.
		void Draw(RendererService& renderer, const Ref<CommandContext>& ctx, uint32_t frameIndex,
		          const RendererService::TonemapParams& params, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
	};
}
