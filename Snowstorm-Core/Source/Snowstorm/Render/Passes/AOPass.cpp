#include "AOPass.hpp"

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
		// Mirrors AOCB in AO.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here silently
		// corrupts the AO reconstruction — keep in lockstep with the shader.
		struct AOCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::uvec2 OutSize{0, 0};
			float AORadius = 0.5f;
			float AOIntensity = 1.0f;
			uint32_t FrameCounter = 0;
			glm::uvec3 _Pad{0, 0, 0};
		};

		// Binding indices in AO.comp.hlsl set 0.
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		// binding 2 (former sampler) intentionally unused (#129 Inc 2c); params stays at 3 to match the shader.
		constexpr uint32_t kParamsBinding = 3;
	}

	void AOPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/AO.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load AO compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "AOPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create AO pipeline");

		// #129 Inc 2c: no sampler — AO point-fetches the G-buffer (Load) and does no bindless texture sampling.

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(AOCB), BufferUsage::Uniform, nullptr, true, "AOCB");
		}
	}

	void AOPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const glm::mat4& invViewProj,
	                      const float radius, const float intensity, const uint32_t frameCounter,
	                      const Ref<TextureView>& gbuffer,
	                      const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH)
	{
		if (!ctx || !gbuffer || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		AOCB cb{};
		cb.InvViewProj = invViewProj;
		cb.OutSize = {outW, outH};
		cb.AORadius = radius;
		cb.AOIntensity = intensity;
		cb.FrameCounter = frameCounter;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(AOCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "AO pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "AOSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer); // .xyz normal, .w depth
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);   // storage image (UAV)
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(AOCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Output must be GENERAL for the UAV write; the G-buffer input is already SHADER_READ (the graph
		// declared it Sampled). Transition to Sampled after so the bilateral upsample reads it.
		ctx->TransitionToStorage(output->GetTexture());

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless SceneTLAS (written by TlasBuildSystem)
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);

		ctx->TransitionToSampled(output->GetTexture());
	}
}
