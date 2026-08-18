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
	// scalar occlusion factor [0,1] into the caller's R16F storage output. Occupancy-only (no sun/IBL), but it
	// does read the per-instance geometry table to alpha-test cutout (glTF MASK) occluders in the any-hit path,
	// so foliage doesn't over-occlude through transparent texels. Set 0 = {G-buffer SRV, output UAV, sampler,
	// params CB}; set 3 (bindless Textures[] + TLAS) is gap-filled by the compute pipeline builder, so
	// BindGlobalResources() supplies it. Only dispatched when AoRTActive() (the caller gates). Owns its
	// pipeline + per-frame sets.
	class AOPass final
	{
	public:
		// Dispatch the half-res AO trace into `output` (a Sampled|Storage R16F view sized outW x outH).
		// `gbuffer` is the full-res depth+normal G-buffer color view (.xyz = world normal, .w = NDC depth);
		// the shader samples it by UV. `invViewProj` reconstructs world pos; `radius`/`intensity`/`frameCounter`/
		// `rayCount` drive the trace (rayCount = render.ao.rays, clamped >= 1). `tableAddr` is the geometry-table
		// device address for the cutout any-hit test (0 = table not ready -> occluders treated as solid). Lazily
		// builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& invViewProj,
		              float radius, float intensity, uint32_t frameCounter, uint32_t rayCount, uint64_t tableAddr,
		              const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;                  // wrapping sampler for the cutout alpha lookup (set 0, binding 2)
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
