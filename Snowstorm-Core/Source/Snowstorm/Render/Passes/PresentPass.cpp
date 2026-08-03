#include "PresentPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		// Set 1 (space1), bindings parked high to dodge the material bindings (0/1/2) that
		// Fullscreen.vert.hlsl drags in via Engine.hlsli — same as Sharpen/Fxaa.
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kTexBinding = 4;     // Texture2D SceneTex : t4
		constexpr uint32_t kSamplerBinding = 5; // SamplerState SceneSampler : s5
	}

	void PresentPass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Clamp-to-edge bilinear; a tap at the image edge must not wrap. Bilinear also handles the case where
		// the source present target and the swapchain differ in size (until #146 makes them track).
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "PresentSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create Present sampler");
	}

	void PresentPass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Fullscreen.vert.hlsl", "Engine/Shaders/Present.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Present shader");

		if (!shader->IsReady())
		{
			return; // async compile; Draw null-guards and retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat}; // the SWAPCHAIN format, not the sRGB present format
		p.DepthFormat = PixelFormat::Unknown; // no depth on the swapchain
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.DebugName = "PresentPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Present pipeline");
		m_ColorFormat = colorFormat;
	}

	void PresentPass::Draw(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                       const Ref<TextureView>& srcSampleView, const PixelFormat colorFormat)
	{
		if (!ctx || !srcSampleView)
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
		}

		if (!m_Sets[frameIndex])
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "Present pipeline missing set=1 layout");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Present_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);
			m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		}

		// The source view changes on resize / scene load, so refresh the texture binding every Draw.
		m_Sets[frameIndex]->SetTexture(kTexBinding, srcSampleView);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx->Draw(3, 1, 0);
	}
}
