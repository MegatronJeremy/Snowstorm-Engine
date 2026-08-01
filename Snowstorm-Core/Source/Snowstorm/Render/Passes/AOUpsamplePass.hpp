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

	// Depth+normal-aware (joint bilateral) upsample of the half-res AO factor to full res (#126). The scalar
	// twin of GIUpsamplePass: a fullscreen pass reading the half-res AO target + the full-res depth+normal
	// G-buffer guide, writing the full-res AO target the forward pass then samples. Rejects taps across
	// depth/normal discontinuities so AO doesn't leak past silhouettes. Kept a separate pass from GIUpsample
	// (per the HDRP/Unreal structure — AO and GI upsample independently, with their own tuning). Owns its
	// pipeline, sampler, and per-frame descriptor sets/UBOs.
	class AOUpsamplePass final
	{
	public:
		// Upsample `ao` (half-res, aoW x aoH) into the bound full-res target, guided by `gbuffer` (full-res
		// .xyz normal, .w depth). colorFormat is the destination's color format (for the pipeline). near/far
		// linearize the guide's NDC depth for the relative edge-stop; depthSigma is its tightness
		// (render.rt.depthsigma). Lazily builds the pipeline (async shader); no-op until ready.
		void Draw(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& ao,
		          const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth, uint32_t aoW, uint32_t aoH,
		          float nearPlane, float farPlane, float depthSigma, PixelFormat colorFormat);

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
