#include "SSRPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Snowstorm
{
	namespace
	{
		// Coarse march + refine budget. Fixed (not CVars) for the baseline; promote to CVars if tuning demands.
		constexpr uint32_t kMaxSteps = 64;
		constexpr uint32_t kRefineSteps = 5;
		// Thickness is scaled to the coarse step so a crossing between two steps is always inside the window
		// (world-space uniform steps -> the gap between samples grows with distance; 2x the step is a safe catch).
		constexpr float kThicknessScale = 2.0f;

		// Mirrors SSRCB in SSR.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). Keep in lockstep.
		struct SSRCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::mat4 ViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float ReflRange = 40.0f;

			glm::uvec2 OutSize{0, 0};
			float Near = 0.1f;
			float Far = 500.0f;

			float Thickness = 1.0f;
			uint32_t MaxSteps = kMaxSteps;
			uint32_t RefineSteps = kRefineSteps;
			uint32_t PrefilteredCubeIndex = 0;
		};

		// Binding indices in SSR.comp.hlsl set 0.
		constexpr uint32_t kShadingBinding = 0;
		constexpr uint32_t kDepthBinding = 1;
		constexpr uint32_t kPrevColorBinding = 2;
		constexpr uint32_t kVelocityBinding = 3;
		constexpr uint32_t kOutputBinding = 4;
		constexpr uint32_t kSamplerBinding = 5;
		constexpr uint32_t kParamsBinding = 6;
	}

	void SSRPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/SSR.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load SSR compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "SSRPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create SSR pipeline");

		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "SSRSampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(SSRCB), BufferUsage::Uniform, nullptr, true, "SSRCB");
		}
	}

	void SSRPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const glm::mat4& viewProj,
	                       const glm::vec3& camPos, const float reflRange, const float nearPlane, const float farPlane,
	                       const uint32_t prefilteredCubeIndex, const Ref<TextureView>& shadingNormal,
	                       const Ref<TextureView>& depth, const Ref<TextureView>& prevColor,
	                       const Ref<TextureView>& velocity, const Ref<TextureView>& output, const uint32_t outW,
	                       const uint32_t outH)
	{
		if (!ctx || !shadingNormal || !depth || !prevColor || !velocity || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		SSRCB cb{};
		cb.InvViewProj = glm::inverse(viewProj);
		cb.ViewProj = viewProj;
		cb.CameraPosition = camPos;
		cb.ReflRange = reflRange;
		cb.OutSize = {outW, outH};
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.MaxSteps = kMaxSteps;
		cb.RefineSteps = kRefineSteps;
		cb.Thickness = (reflRange / static_cast<float>(kMaxSteps)) * kThicknessScale;
		cb.PrefilteredCubeIndex = prefilteredCubeIndex;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(SSRCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "SSR pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "SSRSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kShadingBinding, shadingNormal);
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);
		m_Sets[frameIndex]->SetTexture(kPrevColorBinding, prevColor);
		m_Sets[frameIndex]->SetTexture(kVelocityBinding, velocity);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(SSRCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Graph-managed transitions: the effect declares the inputs in .Reads (-> Sampled) and the output in
		// .Writes (-> Storage). Set 3 (bindless Cubemaps[]) is gap-filled and bound by BindGlobalResources.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless textures/cubemaps (no TLAS use -> non-RT safe)
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
