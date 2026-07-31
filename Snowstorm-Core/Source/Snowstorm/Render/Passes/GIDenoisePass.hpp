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

	// Spatial denoiser for the half-res RT GI (#125). Runs GIDenoise.comp.hlsl — one edge-avoiding à-trous
	// wavelet iteration over the half-res GI irradiance, guided by the full-res depth+normal G-buffer. One
	// Dispatch = one iteration; the caller (GIDenoiseEffect) invokes it N times with a doubling stride,
	// ping-ponging GITarget <-> the denoise scratch, so the blur widens each pass at a fixed 5x5 tap count.
	// Depth+normal edge-stopping only (the spatial half of SVGF; the temporal half stays with TAA). Set 0 =
	// {GI SRV, guide SRV, output UAV, sampler, params CB}; no set 3 (no TLAS/bindless — a plain image filter).
	// Structurally a strict subset of GIPass. Owns nothing but its pipeline + per-frame descriptor sets/UBOs.
	class GIDenoisePass final
	{
	public:
		// One à-trous iteration: read `input` (half-res GI, Sampled) + `gbuffer` (full-res guide: .xyz normal,
		// .w NDC depth), write `output` (half-res GI, Storage). `step` is the tap stride (1,2,4,…). `input` and
		// `output` are the ping-pong pair (same extent). `frameIndex` selects the per-frame descriptor set/UBO;
		// `slot` disambiguates the descriptor set WITHIN a frame (multiple iterations run per frame, each needs
		// its own set). Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, uint32_t slot, int step,
		              const Ref<TextureView>& input, const Ref<TextureView>& gbuffer,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH, float lumaPhi = 0.0f);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		// Per (frame, slot): the denoiser dispatches multiple iterations per frame, so one set/UBO per frame is
		// not enough — index by frameIndex * kMaxSlots + slot. kMaxSlots caps ClampedGIDenoiseIterations() (5).
		std::vector<Ref<Buffer>> m_ParamBuffers;
		std::vector<Ref<DescriptorSet>> m_Sets;
	};
}
