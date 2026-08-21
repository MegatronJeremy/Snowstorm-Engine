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
	struct LightDataBlock;

	// Half-resolution STOCHASTIC direct-shadow compute pass (MegaLights-lite). Runs Shadow.comp.hlsl over the
	// depth+normal G-buffer at the shadow scale, importance-sampling ONE light per half-res pixel (proportional
	// to its unshadowed contribution) and tracing ONE shadow ray, writing an unbiased estimate of the
	// contribution-weighted aggregate shadow ratio [0,1]. The forward pass multiplies full-res unshadowed direct
	// lighting by the temporally-accumulated + denoised + upsampled ratio, instead of the inline per-light
	// RayQueries (~1.7ms after range-cull, the dominant Forward RT cost). Constant 1 ray/pixel regardless of
	// light count. Set 0 = {G-buffer SRV, output UAV, params CB}; set 3 (bindless TLAS) is gap-filled by the
	// compute pipeline builder. Only dispatched when RT shadows are active (the caller gates).
	class RTShadowPass final
	{
	public:
		// Dispatch the half-res stochastic shadow-ratio estimate into `output` (a Sampled|Storage RGBA16F view,
		// outW x outH). `invViewProj` reconstructs world pos; `lights` supplies every light's tracer + importance
		// params (positions/dirs/ranges/cones/luma + per-light cast masks); `normalBias` offsets the ray origin
		// off the surface (acne guard); `frameCounter` rotates the per-pixel random stream (the temporal stage
		// converges the 1 ray/pixel). Lazily builds the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& invViewProj,
		              const LightDataBlock& lights, float normalBias, uint32_t frameCounter, bool soft,
		              float sunTanAngular, float sourceRadius, uint32_t rayCount, uint64_t tableAddr,
		              const glm::vec3& cameraPosition, const Ref<TextureView>& gbuffer,
		              const Ref<TextureView>& shadingNormal, const Ref<TextureView>& depth,
		              const Ref<TextureView>& output, const Ref<TextureView>& specOutput, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;                  // wrapping sampler for the cutout alpha lookup (set 0, binding 2)
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
