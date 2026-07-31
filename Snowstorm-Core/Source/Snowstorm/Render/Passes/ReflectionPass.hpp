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

	// Full-resolution RT reflection compute pass (#129). Runs Reflection.comp.hlsl over the depth+normal
	// G-buffer: per full-res pixel, reconstruct world position from depth + InvViewProj, reflect the view
	// vector, trace ONE sharp reflection ray against the bindless SceneTLAS, shade the hit through the
	// geometry table (or the prefiltered sky on a miss), and write raw reflected radiance (.rgb) + hit
	// distance (.a). The forward pass applies the Fresnel/BRDF weight + ReflIntensity + roughness falloff
	// per-pixel, so this buffer stays a pure radiance signal a temporal denoiser can accumulate. Structural
	// sibling of GIPass: set 0 = {gbuffer SRV, output UAV, sampler, params CB}; set 3 (bindless + TLAS) is
	// supplied by BindGlobalResources(). Only dispatched when ReflectionsRTActive() && the geometry table
	// exists (the caller gates). Owns nothing but its pipeline + per-frame descriptor sets/UBOs.
	class ReflectionPass final
	{
	public:
		// Dispatch the full-res reflection trace into `output` (a Sampled|Storage RGBA16F view sized outW x
		// outH). `gbuffer` is the main G-buffer color view (.xy geometric oct normal, .z roughness, .w depth) —
		// this pass reads depth from it; `shadingNormal` is the separate shading-normal target (.xy oct
		// NORMAL-MAPPED normal, #129 Inc 1c) the reflection reflects off. `frame` supplies camera/sun/IBL
		// params. `tableAddr` is the geometry table device address (0 => hits fall back to sky). Lazily builds
		// the pipeline (async shader); no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const FrameData& frame,
		              uint64_t tableAddr, uint32_t frameCounter,
		              const Ref<TextureView>& gbuffer, const Ref<TextureView>& shadingNormal,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
