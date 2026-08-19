#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;
	class Buffer;

	// Depth+normal-aware bilateral blur of the half-res SSAO factor (#151). Runs SSAOBlur.comp.hlsl over the AO
	// grid: AOTarget -> AOBlurTarget, removing the SSAO kernel-rotation noise while stopping at
	// silhouettes/creases (edge-stopping on the full-res G-buffer normal + depth). The SSAO denoiser —
	// deliberately a plain spatial bilateral blur, not the SVGF chain the RT path uses. Set 0 = {AO SRV,
	// G-buffer SRV, depth SRV, output UAV, params CB}; no set 3. Owns its pipeline + per-frame sets.
	class SSAOBlurPass final
	{
	public:
		// Blur `aoIn` (half-res SSAO factor) into `output` (Sampled|Storage RGBA16F, same size). `gbuffer`/`depth`
		// are the full-res guide; `near`/`far` linearize NDC depth and `depthSigma` (render.rt.depthsigma) is the
		// relative view-depth edge-stop tightness. Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& aoIn,
		              const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth, const Ref<TextureView>& output,
		              uint32_t outW, uint32_t outH, float nearPlane, float farPlane, float depthSigma);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
