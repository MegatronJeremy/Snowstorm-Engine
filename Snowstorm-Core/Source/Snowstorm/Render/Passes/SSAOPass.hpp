#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;
	class Buffer;

	// Screen-space ambient-occlusion compute pass (#151), the raster baseline twin of AOPass. Runs
	// SSAO.comp.hlsl over the depth+normal G-buffer at render.ao.scale: per half-res pixel, reconstruct world
	// position from depth + InvViewProj, sample a normal-oriented hemisphere kernel of the depth buffer,
	// range-check, and write a scalar occlusion factor [0,1] into the caller's RGBA16F storage output (the same
	// AOTarget the RT path writes). Set 0 = {G-buffer SRV, depth SRV, output UAV, params CB}; there is NO set 3
	// (SSAO is not ray traced), so no BindGlobalResources. Only dispatched when AoSSAOActive() (the caller
	// gates). Owns its pipeline + per-frame sets.
	class SSAOPass final
	{
	public:
		// Dispatch the half-res SSAO trace into `output` (a Sampled|Storage RGBA16F view sized outW x outH).
		// `gbuffer` = full-res depth+normal G-buffer color view; `depth` = the fp32 D32 depth view. `invViewProj`
		// reconstructs world pos; `viewProj` projects each kernel sample back to screen. `radius`/`intensity` map
		// to render.ao.radius/intensity; `near`/`far` linearize NDC depth for the range test; `bias` is the
		// view-depth self-occlusion bias. Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& invViewProj,
		              const glm::mat4& viewProj, float radius, float intensity, float nearPlane, float farPlane,
		              float bias, const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
