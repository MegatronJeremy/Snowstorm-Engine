#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// Bake the split-sum IBL maps from the procedural sky on compute (#52): sky -> env cube ->
	// cosine-convolved irradiance cube + GGX-prefiltered (roughness mips) cube + a 2D BRDF integration
	// LUT. Owns all four compute pipelines and the generated cubes/LUT so they survive across frames and
	// tear down in the normal device-shutdown order. The baked maps' bindless indices feed FrameCB
	// (consumed by DefaultLit's ComputeIBL); the renderer pulls them via SetIBLData each frame.
	class IBLBakePass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "IBLBake"; }

		// Bake the maps from the given sky (sun = lights.Lights[0]). Lazy: bakes once, no-ops thereafter.
		// Call inside a recording command buffer with the GPU drained (the bake updates the bindless set;
		// in-flight frames reading it would corrupt — RenderSystem does Renderer::WaitIdle() first).
		void Bake(const Ref<CommandContext>& commandContext,
		          const LightDataBlock& lights,
		          const EnvironmentDataBlock& environment);

		[[nodiscard]] bool IsBaked() const { return m_Baked; }

		// Bindless indices of the baked maps (valid once IsBaked()). 0 before the bake.
		[[nodiscard]] uint32_t IrradianceIndex() const;
		[[nodiscard]] uint32_t PrefilteredIndex() const;
		[[nodiscard]] uint32_t BRDFLutIndex() const;
		[[nodiscard]] uint32_t PrefilteredMipCount() const;

	private:
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
