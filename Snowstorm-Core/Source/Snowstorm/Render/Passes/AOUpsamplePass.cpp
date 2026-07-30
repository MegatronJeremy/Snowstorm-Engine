#include "AOUpsamplePass.hpp"

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
		// Set-1 bindings in AOUpsample.frag.hlsl (parked high to dodge the shared fullscreen VS's material set).
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kAOBinding = 4;
		constexpr uint32_t kGBufferBinding = 5;
		constexpr uint32_t kSamplerBinding = 6;
		constexpr uint32_t kParamsBinding = 7;

		// Mirrors AOUpsampleCB in AOUpsample.frag.hlsl.
		struct AOUpsampleCB
		{
			glm::uvec2 AOSize{0, 0};
			glm::uvec2 FullSize{0, 0};
		};
	}

	void AOUpsamplePass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "AOUpsampleSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create AO upsample sampler");
	}

	void AOUpsamplePass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Fullscreen.vert.hlsl", "Engine/Shaders/AOUpsample.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load AOUpsample shader");
		if (!shader->IsReady())
		{
			return; // async compile; Draw retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown;
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.DebugName = "AOUpsamplePipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create AOUpsample pipeline");
		m_ColorFormat = colorFormat;
	}

	void AOUpsamplePass::Draw(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Ref<TextureView>& ao,
	                          const Ref<TextureView>& gbuffer, const uint32_t aoW, const uint32_t aoH, const PixelFormat colorFormat)
	{
		if (!ctx || !ao || !gbuffer)
		{
			return;
		}

		EnsureSampler();
		EnsurePipeline(colorFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Sets.size() < frames)
		{
			m_Sets.resize(frames);
			m_ParamBuffers.resize(frames);
		}
		if (!m_ParamBuffers[frameIndex])
		{
			m_ParamBuffers[frameIndex] = Buffer::Create(sizeof(AOUpsampleCB), BufferUsage::Uniform, nullptr, true, "AOUpsampleCB");
		}
		if (!m_Sets[frameIndex])
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "AOUpsample pipeline missing set=1 layout");
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "AOUpsample_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);
		}

		AOUpsampleCB cb{};
		cb.AOSize = {aoW, aoH};
		cb.FullSize = {0, 0}; // resolution-independent (UV-based); kept for parity with the shader layout
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(AOUpsampleCB), 0);

		m_Sets[frameIndex]->SetTexture(kAOBinding, ao);
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(AOUpsampleCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx->Draw(3, 1, 0);
	}
}
