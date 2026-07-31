#include "GIDenoisePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		// Mirrors GIDenoiseCB in GIDenoise.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift
		// here silently corrupts the filter — keep in lockstep with the shader.
		struct GIDenoiseCB
		{
			glm::uvec2 OutSize{0, 0};
			int Step = 1;
			float KNormalPow = 8.0f;

			float KDepthScale = 2000.0f;
			glm::vec3 _Pad{0.0f};
		};

		// Binding indices in GIDenoise.comp.hlsl set 0.
		constexpr uint32_t kGIInBinding = 0;
		constexpr uint32_t kGBufferBinding = 1;
		constexpr uint32_t kOutputBinding = 2;
		constexpr uint32_t kSamplerBinding = 3;
		constexpr uint32_t kParamsBinding = 4;

		// Max à-trous iterations per frame = ClampedGIDenoiseIterations() ceiling. Sizes the per-frame set/UBO
		// pool (one set per iteration, since each iteration binds a different input/output + Step).
		constexpr uint32_t kMaxSlots = 5;

		// Edge-stop sigmas — same values GIUpsample.frag.hlsl uses, so the denoiser and the upsample agree on
		// what an edge is. Normal: high power = sharp crease rejection. Depth: NDC-space, cuts at silhouettes.
		constexpr float kNormalPow = 8.0f;
		constexpr float kDepthScale = 2000.0f;
	}

	void GIDenoisePass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GIDenoise.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load GI denoise compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GIDenoisePipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI denoise pipeline");

		// Clamp-linear sampler for the guide fetch (the GI itself is Load'd at integer texels — sampler unused
		// for it, but the shader declares one binding shared by both; guide sampling is the consumer).
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "GIDenoiseSampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames * kMaxSlots);
		m_Sets.resize(frames * kMaxSlots);
		for (uint32_t i = 0; i < frames * kMaxSlots; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(GIDenoiseCB), BufferUsage::Uniform, nullptr, true, "GIDenoiseCB");
		}
	}

	void GIDenoisePass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const uint32_t slot,
	                             const int step, const Ref<TextureView>& input, const Ref<TextureView>& gbuffer,
	                             const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH)
	{
		if (!ctx || !input || !gbuffer || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		SS_CORE_ASSERT(slot < kMaxSlots, "GI denoise slot exceeds the per-frame pool");
		const uint32_t idx = frameIndex * kMaxSlots + slot;

		GIDenoiseCB cb{};
		cb.OutSize = {outW, outH};
		cb.Step = step;
		cb.KNormalPow = kNormalPow;
		cb.KDepthScale = kDepthScale;
		m_ParamBuffers[idx]->SetData(&cb, sizeof(GIDenoiseCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI denoise pipeline missing set=0 layout");
		if (!m_Sets[idx])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GIDenoiseSet";
			m_Sets[idx] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[idx]->SetTexture(kGIInBinding, input);      // half-res GI to filter
		m_Sets[idx]->SetTexture(kGBufferBinding, gbuffer); // .xyz normal, .w depth guide
		m_Sets[idx]->SetTexture(kOutputBinding, output);   // storage image (UAV)
		m_Sets[idx]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[idx], .Offset = 0, .Range = sizeof(GIDenoiseCB)};
		m_Sets[idx]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[idx]->Commit();

		// Output must be GENERAL for the UAV write; the input GI + guide are already SHADER_READ (the graph
		// declared them Sampled). Transition the output to Sampled after so the next iteration / the upsample
		// reads it. The caller ping-pongs input/output, so each becomes the other's Sampled source next pass.
		ctx->TransitionToStorage(output->GetTexture());

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[idx], 0);
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);

		ctx->TransitionToSampled(output->GetTexture());
	}
}
