#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Snowstorm
{
	class RendererService;
	class DescriptorSet;

	// Depth+normal prepass (#124): re-renders the visible meshes into a partial G-buffer — world-space
	// normal (RGBA16F color) + a SAMPLED D32 depth — from the camera's point of view, BEFORE the forward
	// pass. The half-res RT GI compute pass then reconstructs each receiver's world position from that
	// depth (+ InvViewProj) and reads the normal, the per-pixel data a forward renderer otherwise lacks;
	// the bilateral upsample uses both as edge-stopping guides. Also warms early-z for the forward pass.
	//
	// Owns only the graphics pipeline (mesh vertex layout, 64-byte vertex push constant for the camera
	// view-projection). The caster iteration + DrawMesh accumulation stay in RenderSystem/RendererService;
	// this drives RendererService::DrawBatchesDepthNormal — like the depth-only pass but ALSO binding each
	// batch's material set (1) + bindless (3) so the fragment stage can alpha-mask clip cutout geometry
	// (a phantom solid quad in the GI G-buffer otherwise). Set 0 (FrameCB) is an unbound gap.
	class DepthNormalPass final
	{
	public:
		// Record the depth+normal draw of the renderer's accumulated batches into the bound G-buffer target.
		// `viewProj` is pushed as a 64-byte vertex push constant (see DepthNormal.vert.hlsl); per-object
		// Model comes from the set-2 instance buffer. Lazily builds the pipeline for the given color/depth
		// formats. Call inside the depth+normal render pass after the DrawMesh accumulation.
		void RecordDepthNormal(RendererService& renderer, uint32_t frameIndex, PixelFormat colorFormat,
		                       PixelFormat depthFormat, const glm::mat4& viewProj);

	private:
		void EnsurePipeline(PixelFormat colorFormat, PixelFormat depthFormat);
		// Lazily create the pass-owned sampler + per-frame set-1 descriptor sets (one clamp-linear sampler at
		// binding 0, shared by every batch's alpha-mask tap). Returns the set for `frameIndex`.
		const Ref<DescriptorSet>& EnsureSamplerSet(uint32_t frameIndex);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler;                        // clamp-linear, for the alpha-mask albedo tap
		std::vector<Ref<DescriptorSet>> m_SamplerSets; // set 1 (sampler only), one per frame-in-flight
	};
}
