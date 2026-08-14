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

	// Camera depth prepass (early-Z): renders the visible meshes DEPTH-ONLY from the camera's JITTERED
	// view-projection into the scene depth buffer, BEFORE the forward pass. The forward pass then loads that
	// depth (LESS_EQUAL, depth-write off/on) so the expensive DefaultLit shader runs on visible fragments
	// instead of the ~2x overdraw the metric measured. Alpha-cutout correct (clips in the fragment stage) so
	// depth matches forward on masked geometry.
	//
	// Reuses DepthNormal.vert (the mesh vertex interface + camera-VP push) with a depth-only fragment stage
	// (DepthPrepass.frag) and RendererService::DrawBatchesDepthNormal, but builds a pipeline with NO color
	// attachments. Distinct from DepthNormalPass, which is full-res, unjittered, and writes the G-buffer for
	// GI/AO/reflections. This pass is scene-res, jittered, and exists only to warm forward early-Z.
	class CameraDepthPrepass final
	{
	public:
		// Record the depth-only draw of the renderer's accumulated batches. `viewProj` MUST be the same
		// jittered matrix the forward pass uses (cam.Rt->JitteredViewProjection) or LESS_EQUAL drops visible
		// pixels. Call inside the prepass render pass after the DrawMesh accumulation.
		void RecordDepth(RendererService& renderer, uint32_t frameIndex, PixelFormat depthFormat,
		                 const glm::mat4& viewProj);

	private:
		void EnsurePipeline(PixelFormat depthFormat);
		const Ref<DescriptorSet>& EnsureSamplerSet(uint32_t frameIndex);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;

		Ref<Sampler> m_Sampler;                        // clamp-linear, for the alpha-mask albedo tap
		std::vector<Ref<DescriptorSet>> m_SamplerSets; // set 1 (sampler only), one per frame-in-flight
	};
}
