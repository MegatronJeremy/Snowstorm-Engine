#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class RendererService;

	// Motion-vector pass (#44): re-renders the visible meshes with a shader that outputs per-pixel
	// screen-space velocity ((curr_uv - prev_uv), from this-frame and last-frame projection) into the
	// velocity target's RGBA16F color, depth-tested against its own depth so only the nearest fragment's
	// motion survives. The substrate the temporal upscaler reprojects history by; for now the debug view
	// visualizes it. Only added to the graph when render.debugview != 0 (RenderSystem gates it). Owns the
	// graphics pipeline (mesh vertex layout, 128-byte vertex push constant for the two camera matrices);
	// the caster iteration + DrawMesh accumulation stay in RenderSystem, like the shadow/forward passes.
	class VelocityPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Velocity"; }

		// Record the velocity draw of the renderer's accumulated batches into the bound velocity target.
		// `viewProj`/`prevViewProj` are pushed as a 128-byte vertex push constant (see Velocity.vert.hlsl);
		// per-object Model/PrevModel come from the instance buffer. Lazily builds the pipeline for the
		// given color/depth formats. Call inside the velocity render pass after the DrawMesh accumulation.
		void RecordVelocity(RendererService& renderer, PixelFormat colorFormat, PixelFormat depthFormat,
		                    const glm::mat4& viewProj, const glm::mat4& prevViewProj);

	private:
		void EnsurePipeline(PixelFormat colorFormat, PixelFormat depthFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;
	};
}
