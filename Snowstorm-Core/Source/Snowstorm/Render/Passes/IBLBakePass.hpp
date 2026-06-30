#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// Bake the split-sum IBL maps from the procedural sky on compute (#52): sky -> env cube ->
	// cosine-convolved irradiance cube + GGX-prefiltered (roughness mips) cube + a 2D BRDF integration
	// LUT. Owns all four compute pipelines and the generated cubes/LUT so they survive across frames and
	// tear down in the normal device-shutdown order.
	//
	// The bake is expressed as four graph compute passes (Part 2): each declares the maps it Writes and
	// the env cube it Reads, and the RenderGraph inserts every Storage/Sampled transition at the pass
	// boundaries — there are no hand-called barriers here anymore. The consuming mesh pass declares it
	// Reads{Sampled} the three output maps, so the graph transitions them to shader-read before shading.
	class IBLBakePass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "IBLBake"; }

		// Create the bake resources (once) and append the four bake compute passes to this frame's graph.
		// Call once, on the frame IBL is first enabled, with the GPU drained (resource creation updates the
		// bindless set; RenderSystem does Renderer::WaitIdle() first). The dispatches then run inside
		// graph.Execute, before the mesh pass that samples the maps. No-ops once baked.
		void AddBakePasses(RenderGraph& graph, const LightDataBlock& lights, const EnvironmentDataBlock& environment);

		[[nodiscard]] bool IsBaked() const { return m_Baked; }

		// Bindless indices of the baked maps (valid once IsBaked()). 0 before the bake.
		[[nodiscard]] uint32_t IrradianceIndex() const;
		[[nodiscard]] uint32_t PrefilteredIndex() const;
		[[nodiscard]] uint32_t BRDFLutIndex() const;
		[[nodiscard]] uint32_t PrefilteredMipCount() const;

		// The baked map textures, so the consuming pass can declare Reads{Sampled} on them. Null before bake.
		[[nodiscard]] const Ref<Texture>& IrradianceCube() const { return m_IrradianceCube; }
		[[nodiscard]] const Ref<Texture>& PrefilteredCube() const { return m_PrefilteredCube; }
		[[nodiscard]] const Ref<Texture>& BRDFLut() const { return m_BRDFLut; }

	private:
		// One-time GPU-resource creation (pipelines, cubes, LUT, sampler). Registers the maps' views in the
		// bindless arrays (updates the bindless descriptor set — caller must have drained the GPU first).
		void EnsureResources();

		Ref<Pipeline> m_CapturePipeline;    // sky -> env cube
		Ref<Pipeline> m_IrradiancePipeline; // env cube -> irradiance cube
		Ref<Pipeline> m_PrefilterPipeline;  // env cube -> prefiltered (roughness mips) cube
		Ref<Pipeline> m_BRDFLutPipeline;    // BRDF integration LUT (2D)

		Ref<Texture> m_EnvCube;                // captured sky environment (HDR)
		Ref<TextureView> m_EnvCubeView;        // full-cube sampled view (kept alive; bound during convolution)
		Ref<Texture> m_IrradianceCube;         // diffuse irradiance
		Ref<TextureView> m_IrradianceCubeView; // full-cube sampled view (kept alive; read by FrameCB)
		Ref<Texture> m_PrefilteredCube;        // specular prefiltered env (mip = roughness)
		Ref<TextureView> m_PrefilteredCubeView;
		Ref<Texture> m_BRDFLut; // 2D BRDF integration LUT
		Ref<TextureView> m_BRDFLutView;
		Ref<Sampler> m_Sampler; // linear clamp sampler for the convolution

		bool m_Baked = false;

		// Per-face UBOs / descriptor sets / face views recorded into the bake command buffer; they must
		// outlive the in-flight frame. Parked here for the session (the bake runs once; freeing them needs
		// deferred-deletion infra to avoid a use-after-free on the still-in-flight bake submission).
		std::vector<Ref<void>> m_BakeKeepAlive;
	};
}
