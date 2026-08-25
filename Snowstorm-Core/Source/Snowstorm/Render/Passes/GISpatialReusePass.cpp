#include "GISpatialReusePass.hpp"

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
		// Mirrors SpatialCB in GISpatialReuse.comp.hlsl field-for-field (16-byte cbuffer rows).
		struct SpatialCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::uvec2 OutSize{0, 0};
			float GIIntensity = 1.0f;
			uint32_t FrameCounter = 0;

			float NearPlane = 0.1f;
			float FarPlane = 500.0f;
			float SpatialRadius = 16.0f;
			uint32_t SpatialCount = 4;

			uint32_t CheckVisibility = 1;
			glm::uvec3 _Pad{0, 0, 0};
		};

		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kDepthBinding = 1;
		constexpr uint32_t kResSampleBinding = 2;
		constexpr uint32_t kResRadianceBinding = 3;
		constexpr uint32_t kResNormalBinding = 4;
		constexpr uint32_t kOutputBinding = 5;
		constexpr uint32_t kParamsBinding = 6;
	}

	void GISpatialReusePass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GISpatialReuse.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load GI spatial reuse compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GISpatialReusePipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI spatial reuse pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(SpatialCB), BufferUsage::Uniform, nullptr, true, "GISpatialCB");
		}
	}

	void GISpatialReusePass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const glm::mat4& invViewProj,
	                                  const uint32_t frameCounter, const float giIntensity, const float nearPlane, const float farPlane,
	                                  const float radius, const uint32_t neighbourCount, const bool checkVisibility,
	                                  const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
	                                  const Ref<TextureView>& reservoirSample, const Ref<TextureView>& reservoirRadiance,
	                                  const Ref<TextureView>& reservoirNormal,
	                                  const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH)
	{
		if (!ctx || !gbuffer || !depth || !reservoirSample || !reservoirRadiance || !reservoirNormal || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return;
		}

		SpatialCB cb{};
		cb.InvViewProj = invViewProj;
		cb.OutSize = {outW, outH};
		cb.GIIntensity = giIntensity;
		cb.FrameCounter = frameCounter;
		cb.NearPlane = nearPlane;
		cb.FarPlane = farPlane;
		cb.SpatialRadius = radius;
		cb.SpatialCount = neighbourCount;
		cb.CheckVisibility = checkVisibility ? 1u : 0u;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(SpatialCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI spatial reuse pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GISpatialReuseSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);
		m_Sets[frameIndex]->SetTexture(kResSampleBinding, reservoirSample);
		m_Sets[frameIndex]->SetTexture(kResRadianceBinding, reservoirRadiance);
		m_Sets[frameIndex]->SetTexture(kResNormalBinding, reservoirNormal);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(SpatialCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless textures/cubemaps + SceneTLAS
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
