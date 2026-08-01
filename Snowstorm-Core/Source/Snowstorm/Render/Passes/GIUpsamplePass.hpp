#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;
	class Buffer;

	// Depth+normal-aware (joint bilateral) upsample of the half-res GI irradiance to full res (#124). A
	// fullscreen pass reading the half-res GI target + the full-res depth+normal G-buffer guide, writing the
	// full-res GI target the forward pass then samples. Rejects taps across depth/normal discontinuities so
	// GI doesn't leak past silhouettes (the artifact a plain bilinear upscale causes). Structure mirrors
	// UpscalePass (self-contained set-1 resources at high bindings, fullscreen triangle); the bilateral logic
	// + guide input are net-new. Owns its pipeline, sampler, and per-frame descriptor sets/UBOs.
	class GIUpsamplePass final
	{
	public:
		// Upsample `gi` (half-res, giW x giH) into the bound full-res target, guided by `gbuffer` (full-res
		// .xyz normal, .w depth). colorFormat is the destination's color format (for the pipeline). near/far
		// linearize the guide's NDC depth for the relative edge-stop; depthSigma is its tightness
		// (render.rt.depthsigma). Lazily builds the pipeline (async shader); no-op until ready.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& gi,
		          const Ref<TextureView>& gbuffer, uint32_t giW, uint32_t giH, float nearPlane, float farPlane,
		          float depthSigma, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
