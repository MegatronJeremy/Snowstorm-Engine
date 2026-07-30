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

	// Half-resolution RT ambient-occlusion compute pass (#126). Runs AO.comp.hlsl over the depth+normal
	// G-buffer at render.ao.scale: per half-res pixel, reconstruct world position from depth + InvViewProj,
	// trace AO_RAY_COUNT short cosine-hemisphere occupancy rays against the bindless SceneTLAS, and write a
	// scalar occlusion factor [0,1] into the caller's R16F storage output. A strict subset of GIPass — no
	// geometry table, no sun/IBL params (AO is occupancy-only). Set 0 = {G-buffer SRV, output UAV, sampler,
	// params CB}; set 3 (bindless TLAS) is gap-filled by the compute pipeline builder, so BindGlobalResources()
	// supplies it. Only dispatched when AoRTActive() (the caller gates). Owns its pipeline + per-frame sets.
	class AOPass final
	{
	public:
		// Dispatch the half-res AO trace into `output` (a Sampled|Storage R16F view sized outW x outH).
		// `gbuffer` is the full-res depth+normal G-buffer color view (.xyz = world normal, .w = NDC depth);
		// the shader samples it by UV. `invViewProj` reconstructs world pos; `radius`/`intensity`/`frameCounter`
		// drive the trace. Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& invViewProj,
		              float radius, float intensity, uint32_t frameCounter,
		              const Ref<TextureView>& gbuffer,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
