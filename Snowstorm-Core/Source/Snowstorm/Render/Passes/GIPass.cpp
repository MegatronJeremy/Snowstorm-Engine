#include "GIPass.hpp"

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
		// Mirrors GICB in GI.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here silently
		// corrupts the GI reconstruction — keep in lockstep with the shader.
		struct GICB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::mat4 ViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float GIRange = 8.0f;

			glm::uvec2 OutSize{0, 0};
			float GIIntensity = 1.0f;
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
			glm::uvec2 _Pad{0, 0};
		};

		// Binding indices in GI.comp.hlsl set 0 (one packed G-buffer color: .xyz normal, .w depth).
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		constexpr uint32_t kSamplerBinding = 2;
		constexpr uint32_t kParamsBinding = 3;
	}

	void GIPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GI.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load GI compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GIPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI pipeline");

		// Clamp-linear sampler for the bindless albedo / cubemap fetches in the hit shading.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "GISampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(GICB), BufferUsage::Uniform, nullptr, true, "GICB");
		}
	}

	void GIPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const FrameData& frame,
	                      const uint64_t tableAddr, const uint32_t frameCounter,
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

		// Fill the params from the frame's camera/sun/IBL + the GI CVars.
		GICB cb{};
		cb.InvViewProj = glm::inverse(frame.ViewProjection);
		cb.ViewProj = frame.ViewProjection;
		cb.CameraPosition = frame.CameraPosition;
		cb.GIRange = CVars::GIRange.Get();
		cb.OutSize = {outW, outH};
		cb.GIIntensity = CVars::GIIntensity.Get();
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

		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(GICB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GISet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer); // .xyz normal, .w depth
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);  // storage image (UAV)
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(GICB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Output must be GENERAL for the UAV write; the G-buffer inputs are already SHADER_READ (the graph
		// declared them Sampled). Transition to Sampled after so the bilateral upsample (Inc 3) reads it.
		ctx->TransitionToStorage(output->GetTexture());

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless textures/cubemaps + SceneTLAS (written by TlasBuildSystem)
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);

		ctx->TransitionToSampled(output->GetTexture());
	}
}
