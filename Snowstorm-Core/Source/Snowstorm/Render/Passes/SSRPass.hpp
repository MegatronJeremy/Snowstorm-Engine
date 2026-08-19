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

	// Screen-space reflection compute pass (#151), the raster baseline twin of ReflectionPass. Runs
	// SSR.comp.hlsl over the depth+shading-normal G-buffer at full res: per pixel, reconstruct the receiver
	// world position, reflect off the shading normal, march the depth buffer, and write raw reflected radiance
	// (.rgb) + hit distance (.a) into the caller's RGBA16F output (the same ReflectionTarget the RT path writes).
	// On a screen-space hit it samples the previous frame's scene color (reprojected by velocity); on a miss it
	// samples the prefiltered env cube. Set 0 = {shading-normal SRV, depth SRV, prev-color SRV, velocity SRV,
	// output UAV, sampler, params CB}; set 3 = bindless Cubemaps[] (gap-filled; no TLAS, so it stays non-RT).
	// Only dispatched when ReflectionsSSRActive() (the caller gates). Owns its pipeline + per-frame sets.
	class SSRPass final
	{
	public:
		// Dispatch the full-res SSR trace into `output`. `viewProj` is THIS frame's jittered camera VP (the pass
		// inverts it for world reconstruction and reuses it to project march samples). `camPos` gives the view
		// vector; `reflRange` (render.reflections.range) bounds the ray; `near`/`far` linearize NDC depth for the
		// crossing/thickness test; `prefilteredCubeIndex` is the bindless env cube for the miss. Views:
		// `shadingNormal` (oct shading normal), `depth` (fp32 D32), `prevColor` (previous frame's HDR scene color),
		// `velocity` (screen motion for reprojection). Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& viewProj,
		              const glm::vec3& camPos, float reflRange, float nearPlane, float farPlane,
		              uint32_t prefilteredCubeIndex, const Ref<TextureView>& shadingNormal, const Ref<TextureView>& depth,
		              const Ref<TextureView>& prevColor, const Ref<TextureView>& velocity, const Ref<TextureView>& output,
		              uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;                  // clamp-linear for prev-color + cubemap sampling
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
