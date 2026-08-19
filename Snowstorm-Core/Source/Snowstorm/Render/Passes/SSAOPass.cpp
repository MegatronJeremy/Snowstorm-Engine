#include "SSAOPass.hpp"

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
		// Mirrors SSAOCB in SSAO.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here silently
		// corrupts the reconstruction, so keep in lockstep with the shader.
		struct SSAOCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::mat4 ViewProj{1.0f};
			glm::uvec2 OutSize{0, 0};
			float AORadius = 0.5f;
			float AOIntensity = 1.0f;
			float Near = 0.1f;
			float Far = 500.0f;
			float Bias = 0.025f;
			float _Pad0 = 0.0f;
			float _Pad1 = 0.0f;
			float _Pad2 = 0.0f;
		};

		// Binding indices in SSAO.comp.hlsl set 0.
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		constexpr uint32_t kParamsBinding = 3;
		constexpr uint32_t kDepthBinding = 4;
	}

	void SSAOPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/SSAO.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load SSAO compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "SSAOPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create SSAO pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(SSAOCB), BufferUsage::Uniform, nullptr, true, "SSAOCB");
		}
	}

	void SSAOPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const glm::mat4& invViewProj,
	                        const glm::mat4& viewProj, const float radius, const float intensity, const float nearPlane,
	                        const float farPlane, const float bias, const Ref<TextureView>& gbuffer,
	                        const Ref<TextureView>& depth, const Ref<TextureView>& output, const uint32_t outW,
	                        const uint32_t outH)
	{
		if (!ctx || !gbuffer || !depth || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		SSAOCB cb{};
		cb.InvViewProj = invViewProj;
		cb.ViewProj = viewProj;
		cb.OutSize = {outW, outH};
		cb.AORadius = radius;
		cb.AOIntensity = intensity;
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.Bias = bias;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(SSAOCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "SSAO pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "SSAOSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(SSAOCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions are graph-managed: the effect declares the output in .Writes (-> Storage); the blur
		// pass's .Reads does the read-back to Sampled. Set-0-only, so no BindGlobalResources (no TLAS).
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
