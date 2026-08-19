#include "SSAOBlurPass.hpp"

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
		// Mirrors SSAOBlurCB in SSAOBlur.comp.hlsl (std140/cbuffer 16-byte rows). Keep in lockstep.
		struct SSAOBlurCB
		{
			glm::uvec2 OutSize{0, 0};
			float Near = 0.1f;
			float Far = 500.0f;
			float DepthSigma = 50.0f;
			float _Pad0 = 0.0f;
			float _Pad1 = 0.0f;
			float _Pad2 = 0.0f;
		};

		// Binding indices in SSAOBlur.comp.hlsl set 0.
		constexpr uint32_t kAOBinding = 0;
		constexpr uint32_t kGBufferBinding = 1;
		constexpr uint32_t kDepthBinding = 2;
		constexpr uint32_t kOutputBinding = 3;
		constexpr uint32_t kParamsBinding = 4;
	}

	void SSAOBlurPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/SSAOBlur.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load SSAOBlur compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "SSAOBlurPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create SSAOBlur pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(SSAOBlurCB), BufferUsage::Uniform, nullptr, true, "SSAOBlurCB");
		}
	}

	void SSAOBlurPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Ref<TextureView>& aoIn,
	                            const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
	                            const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH,
	                            const float nearPlane, const float farPlane, const float depthSigma)
	{
		if (!ctx || !aoIn || !gbuffer || !depth || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		SSAOBlurCB cb{};
		cb.OutSize = {outW, outH};
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.DepthSigma = depthSigma;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(SSAOBlurCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "SSAOBlur pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "SSAOBlurSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kAOBinding, aoIn);
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(SSAOBlurCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Graph-managed transitions: the effect declares aoIn/gbuffer/depth in .Reads (-> Sampled) and output in
		// .Writes (-> Storage). Set-0-only, so no BindGlobalResources.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
