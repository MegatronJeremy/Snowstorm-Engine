#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class RendererService;

	// Depth+normal prepass (#124): re-renders the visible meshes into a partial G-buffer — world-space
	// normal (RGBA16F color) + a SAMPLED D32 depth — from the camera's point of view, BEFORE the forward
	// pass. The half-res RT GI compute pass then reconstructs each receiver's world position from that
	// depth (+ InvViewProj) and reads the normal, the per-pixel data a forward renderer otherwise lacks;
	// the bilateral upsample uses both as edge-stopping guides. Also warms early-z for the forward pass.
	//
	// Owns only the graphics pipeline (mesh vertex layout, 64-byte vertex push constant for the camera
	// view-projection — the same shape the shadow pass uses). The caster iteration + DrawMesh accumulation
	// stay in RenderSystem/RendererService, so this reuses RendererService::DrawBatchesDepthOnly verbatim
	// (that primitive binds only set 2 + pushes one matrix, which is exactly this pass's interface).
	class DepthNormalPass final
	{
	public:
		// Record the depth+normal draw of the renderer's accumulated batches into the bound G-buffer target.
		// `viewProj` is pushed as a 64-byte vertex push constant (see DepthNormal.vert.hlsl); per-object
		// Model comes from the set-2 instance buffer. Lazily builds the pipeline for the given color/depth
		// formats. Call inside the depth+normal render pass after the DrawMesh accumulation.
		void RecordDepthNormal(RendererService& renderer, PixelFormat colorFormat, PixelFormat depthFormat,
		                       const glm::mat4& viewProj);

	private:
		void EnsurePipeline(PixelFormat colorFormat, PixelFormat depthFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;
	};
}
