#include "ReflectionPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/FrameData.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Snowstorm
{
	namespace
	{
		// Mirrors ReflCB in Reflection.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here
		// silently corrupts the reflection trace — keep in lockstep with the shader.
		struct ReflCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float ReflRange = 40.0f;

			glm::uvec2 OutSize{0, 0};
			float ReflConeScale = 1.0f;
			uint32_t FrameCounter = 0;

			glm::vec3 SunDirection{0.0f};
			float SunIntensity = 0.0f;
			glm::vec3 SunColor{0.0f};
			float ShadowStrength = 1.0f;

			uint32_t IrradianceCubeIndex = 0;
			uint32_t PrefilteredCubeIndex = 0;
			float IBLIntensity = 1.0f;
			uint32_t LightCount = 0;

			uint32_t ReflGeoTableAddrLo = 0;
			uint32_t ReflGeoTableAddrHi = 0;
			uint32_t RayCount = 1; // render.reflections.rays (clamped) — reflection rays/pixel this frame
			uint32_t _Pad1 = 0;
		};

		// The shader's own cbuffer rows sum to this. Catches the whole-struct half of a drift at compile time
		// (a per-field offset shuffle that preserves the total still slips through, so read the shader when
		// reordering).
		static_assert(sizeof(ReflCB) == 160, "ReflCB no longer matches ReflCB in Reflection.comp.hlsl");

		// Binding indices in Reflection.comp.hlsl set 0 (#129 Inc 1c added the shading-normal SRV at 1).
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kShadingBinding = 1;
		constexpr uint32_t kOutputBinding = 2;
		constexpr uint32_t kSamplerBinding = 3;
		constexpr uint32_t kParamsBinding = 4;
		constexpr uint32_t kDepthBinding = 5; // fp32 D32 depth SRV (was packed in the G-buffer .w)
	}

	void ReflectionPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/Reflection.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load reflection compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "ReflectionPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create reflection pipeline");

		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "ReflectionSampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(ReflCB), BufferUsage::Uniform, nullptr, true, "ReflCB");
		}
	}

	void ReflectionPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const FrameData& frame,
	                              const uint64_t tableAddr, const uint32_t frameCounter,
	                              const Ref<TextureView>& gbuffer, const Ref<TextureView>& shadingNormal,
	                              const Ref<TextureView>& depth,
	                              const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH)
	{
		if (!ctx || !gbuffer || !shadingNormal || !depth || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		ReflCB cb{};
		cb.InvViewProj = glm::inverse(frame.ViewProjection);
		cb.CameraPosition = frame.CameraPosition;
		cb.ReflRange = CVars::ReflectionRange.Get();
		cb.OutSize = {outW, outH};
		cb.ReflConeScale = CVars::ReflectionConeScale.Get();
		cb.FrameCounter = frameCounter;

		cb.LightCount = static_cast<uint32_t>(frame.Lights.LightCount);
		if (frame.Lights.LightCount > 0)
		{
			cb.SunDirection = frame.Lights.Lights[0].Direction;
			cb.SunIntensity = frame.Lights.Lights[0].Intensity;
			cb.SunColor = frame.Lights.Lights[0].Color;
		}
		cb.ShadowStrength = CVars::ShadowStrength.Get();

		cb.IrradianceCubeIndex = frame.IBL.IrradianceCubeIndex;
		cb.PrefilteredCubeIndex = frame.IBL.PrefilteredCubeIndex;
		cb.IBLIntensity = CVars::IBLIntensity.Get();

		cb.ReflGeoTableAddrLo = static_cast<uint32_t>(tableAddr & 0xFFFFFFFFull);
		cb.ReflGeoTableAddrHi = static_cast<uint32_t>(tableAddr >> 32);
		cb.RayCount = static_cast<uint32_t>(CVars::ClampedReflectionRayCount());

		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(ReflCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "Reflection pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "ReflectionSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);       // main: geometric normal + roughness
		m_Sets[frameIndex]->SetTexture(kShadingBinding, shadingNormal); // .xy oct shading normal (#129 Inc 1c)
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);           // fp32 D32 depth SRV
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);         // storage image (UAV)
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(ReflCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions graph-managed (#129 Inc 4): the effect declares this output in .Writes (-> Storage);
		// the read-back to Sampled comes from the reflection temporal pass's .Reads. No hand-called transitions.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless textures/cubemaps + SceneTLAS
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
