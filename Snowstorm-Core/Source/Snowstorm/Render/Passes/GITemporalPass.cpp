#include "GITemporalPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	namespace
	{
		// Mirrors GITemporalCB in GITemporal.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift
		// here silently corrupts the reproject — keep in lockstep with the shader.
		struct GITemporalCB
		{
			glm::uvec2 OutSize{0, 0};
			float HistoryValid = 0.0f;
			float BlendHistory = 0.9f;

			float MaxBlend = 0.97f;
			float Near = 0.1f;
			float Far = 500.0f;
			float DepthRejectScale = 0.02f;
		};

		// Binding indices in GITemporal.comp.hlsl set 0.
		constexpr uint32_t kCurrentBinding = 0;
		constexpr uint32_t kGBufferBinding = 1;
		constexpr uint32_t kVelocityBinding = 2;
		constexpr uint32_t kHistoryBinding = 3;
		constexpr uint32_t kOutputBinding = 4;
		constexpr uint32_t kSamplerBinding = 5;
		constexpr uint32_t kParamsBinding = 6;
		constexpr uint32_t kMomentsPrevBinding = 7; // #129 Inc 3c
		constexpr uint32_t kMomentsOutBinding = 8;
	}

	void GITemporalPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GITemporal.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load GI temporal compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GITemporalPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI temporal pipeline");

		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "GITemporalSampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(GITemporalCB), BufferUsage::Uniform, nullptr, true, "GITemporalCB");
		}
	}

	void GITemporalPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                              const Ref<TextureView>& current, const Ref<TextureView>& gbuffer,
	                              const Ref<TextureView>& velocity, const Ref<TextureView>& historyPrev,
	                              const Ref<TextureView>& momentsPrev, const Ref<TextureView>& momentsOut,
	                              const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH,
	                              const bool historyValid, const float blend, const float maxBlend,
	                              const float nearPlane, const float farPlane, const float depthReject)
	{
		if (!ctx || !current || !gbuffer || !velocity || !historyPrev || !momentsPrev || !momentsOut || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		GITemporalCB cb{};
		cb.OutSize = {outW, outH};
		cb.HistoryValid = historyValid ? 1.0f : 0.0f;
		cb.BlendHistory = blend;
		cb.MaxBlend = maxBlend;
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.DepthRejectScale = depthReject;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(GITemporalCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI temporal pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GITemporalSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kCurrentBinding, current);
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetTexture(kVelocityBinding, velocity);
		m_Sets[frameIndex]->SetTexture(kHistoryBinding, historyPrev);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		m_Sets[frameIndex]->SetTexture(kMomentsPrevBinding, momentsPrev); // #129 Inc 3c
		m_Sets[frameIndex]->SetTexture(kMomentsOutBinding, momentsOut);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(GITemporalCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions are graph-managed (#129 Inc 4): the effect declares both outputs (accumulated GI +
		// moments) in the pass's .Writes (-> Storage before the dispatch); the read-back to Sampled comes from
		// the à-trous denoiser's .Reads (color) and NEXT frame's temporal .Reads (moments). No hand transitions.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
