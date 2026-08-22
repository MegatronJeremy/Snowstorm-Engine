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

	// Screen-space global illumination compute pass (#151), the raster baseline twin of GIPass. Runs
	// SSGI.comp.hlsl over the depth + geometric-normal G-buffer at HALF res (render.gi.scale): per pixel,
	// reconstruct the receiver world position, draw RayCount cosine-hemisphere directions, march the depth
	// buffer along each, and on a hit sample the previous frame's scene color (reprojected by velocity) as
	// incoming radiance; on a miss (off-screen, behind-camera, see-through, backfacing) sample the prefiltered
	// env cube, the same fallback GIPass takes. Writes incoming irradiance (.rgb, NO receiver albedo) into the
	// caller's RGBA16F output, the same GITarget the RT path writes, so the shared GI tail (temporal, a-trous,
	// bilateral upsample) and the forward consumption are identical for both producers. Set 0 = {normal SRV,
	// depth SRV, prev-color SRV, velocity SRV, output UAV, sampler, params CB}; set 3 = bindless Cubemaps[]
	// (gap-filled; no TLAS, so it stays non-RT). Only dispatched when GiSSGIActive() (the caller gates).
	class SSGIPass final
	{
	public:
		// Dispatch the half-res SSGI gather into `output`. `viewProj` is THIS frame's jittered camera VP (the
		// pass inverts it for world reconstruction and reuses it to project march samples). `giRange`
		// (render.gi.range) bounds each gather ray; `near`/`far` linearize NDC depth for the crossing/thickness
		// test; `rayCount` (render.gi.rays) is the hemisphere sample count; `frameCounter` rotates the sample
		// pattern so the shared temporal pass converges it; `giIntensity`/`iblIntensity` scale the bounce and the
		// cube miss to match GIPass. Views: `normal` (oct GEOMETRIC normal, ColorAttachments[0]), `depth` (fp32
		// D32), `prevColor` (previous frame's linear HDR scene color), `velocity` (screen motion for
		// reprojection). Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& viewProj,
		              const glm::vec3& camPos, float giRange, float nearPlane, float farPlane, float giIntensity,
		              float iblIntensity, uint32_t rayCount, uint32_t frameCounter, uint32_t prefilteredCubeIndex,
		              const Ref<TextureView>& normal, const Ref<TextureView>& depth, const Ref<TextureView>& prevColor,
		              const Ref<TextureView>& velocity, const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;                  // clamp-linear for prev-color + cubemap sampling
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
