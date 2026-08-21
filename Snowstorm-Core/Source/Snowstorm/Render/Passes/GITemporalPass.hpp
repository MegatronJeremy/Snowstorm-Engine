#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;
	class Buffer;

	// GI temporal accumulation (#125), the temporal half of SVGF. Runs GITemporal.comp.hlsl at half-res
	// BEFORE the à-trous denoiser: reproject the previous accumulated GI by the motion vectors, depth-
	// disocclusion-reject it (reused from the TAA resolve, #127), and blend with this frame's raw GITarget
	// trace with a velocity-aware weight. The output feeds the denoiser AND becomes next frame's history.
	// Set 0 = {GI SRV, guide SRV, velocity SRV, history SRV, output UAV, sampler, params CB}; no set 3
	// (no TLAS/bindless — a plain image op). Structurally a sibling of GIPass. Owns nothing but its pipeline
	// + per-frame descriptor sets/UBOs.
	class GITemporalPass final
	{
	public:
		// Accumulate: read `current` (this frame's raw half-res GI, Sampled), `gbuffer` (full-res guide for
		// the current depth), `velocity` (full-res motion + depth), `historyPrev` (previous accumulated GI:
		// .rgb irradiance), `momentsPrev` (previous SVGF moments: .r μ1, .g μ2, .b histLen, .a prev depth);
		// write `output` (accumulated GI .rgb + variance .a, Storage) and `momentsOut` (this frame's moments).
		// `historyValid` gates the blend (false on the first frame / after a reset -> passes current through).
		// near/far linearize the depths for the disocclusion test; `depthReject` is the relative threshold
		// (0 = off). #129 Inc 3c added the moments in/out for textbook SVGF variance. Lazy pipeline build.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex,
		              const Ref<TextureView>& current, const Ref<TextureView>& gbuffer,
		              const Ref<TextureView>& depth,
		              const Ref<TextureView>& velocity, const Ref<TextureView>& historyPrev,
		              const Ref<TextureView>& momentsPrev, const Ref<TextureView>& momentsOut,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH,
		              bool historyValid, float blend, float maxBlend, float nearPlane, float farPlane,
		              float depthReject, bool neighborhoodClamp);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
