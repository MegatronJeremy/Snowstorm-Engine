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
	struct FrameData;

	// Half-resolution RT GI compute pass (#124). Runs GI.comp.hlsl over the depth+normal G-buffer at
	// render.gi.scale: per half-res pixel, reconstruct world position from depth + InvViewProj, trace the
	// cosine hemisphere against the bindless SceneTLAS, shade hits through the geometry table, and write
	// incoming irradiance (no albedo) into the caller's storage output. Set 0 = {depth SRV, normal SRV,
	// output UAV, sampler, params CB}; set 3 (bindless textures/cubemaps/TLAS) is gap-filled by the compute
	// pipeline builder, so BindGlobalResources() supplies the TLAS — no manual bind. Only dispatched when
	// GIRTActive() && the geometry table exists (the caller gates). Params are pulled from FrameData +
	// EngineCVars; owns nothing but its pipeline + per-frame descriptor sets/UBOs.
	class GIPass final
	{
	public:
		// Dispatch the half-res GI trace into `output` (a Sampled|Storage RGBA16F view sized outW x outH).
		// `gbuffer` is the full-res G-buffer color view (.xy = oct normal, .z = roughness); `depth` is the
		// full-res fp32 D32 depth attachment view (world position reconstructed from it) — depth is no longer
		// packed in the G-buffer .w. `frame` supplies the camera/sun/IBL params. `tableAddr` is the reflection
		// geometry table device address (0 => hits fall back to sky). Lazily builds the pipeline; no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const FrameData& frame,
		              uint64_t tableAddr, uint32_t frameCounter,
		              const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH,
		              const Ref<TextureView>& resSample, const Ref<TextureView>& resRadiance,
		              const Ref<TextureView>& resNormal, const Ref<TextureView>& resSamplePrev,
		              const Ref<TextureView>& resRadiancePrev, const Ref<TextureView>& resNormalPrev,
		              const Ref<TextureView>& velocity, bool reservoirHistoryValid,
		              float nearPlane, float farPlane);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
