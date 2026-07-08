#include "FxaaPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		// Must match FxaaCB in Fxaa.frag.hlsl (float2 RcpFrame + float2 pad = one 16-byte row).
		struct FxaaCB
		{
			glm::vec2 RcpFrame{0.0f};
			glm::vec2 _Pad{0.0f};
		};

		// Set 1 (space1). Bindings parked at 3/4/5 to avoid the material bindings (0/1/2) that
		// Fullscreen.vert.hlsl drags in via Engine.hlsli — see the shader header comment.
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kCbBinding = 3;      // cbuffer FxaaCB : b3
		constexpr uint32_t kTexBinding = 4;     // Texture2D SceneTex : t4
		constexpr uint32_t kSamplerBinding = 5; // SamplerState SceneSampler : s5
	}

	void FxaaPass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Clamp-to-edge bilinear: FXAA does sub-texel taps, and a tap at the image edge must not wrap.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "FxaaSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create FXAA sampler");
	}

	void FxaaPass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "assets/shaders/Fullscreen.vert.hlsl", "assets/shaders/Fxaa.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load FXAA shader");

		if (!shader->IsReady())
		{
			return; // async compile; Draw null-guards and retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown; // no depth on the present target
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.DebugName = "FxaaPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create FXAA pipeline");
		m_ColorFormat = colorFormat;
	}

	void FxaaPass::Draw(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                    const Ref<TextureView>& srcSampleView, const glm::vec2& rcpFrame, const PixelFormat colorFormat)
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
			m_UniformBuffers.resize(frames);
		}

		if (!m_Sets[frameIndex])
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "FXAA pipeline missing set=1 layout");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Fxaa_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);

			m_UniformBuffers[frameIndex] = Buffer::Create(sizeof(FxaaCB), BufferUsage::Uniform, nullptr, true, "FxaaCB");
			const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(FxaaCB)};
			m_Sets[frameIndex]->SetBuffer(kCbBinding, bb);
			m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		}

		// Refresh per-frame: the source view changes on resize; RcpFrame follows it.
		FxaaCB cb{};
		cb.RcpFrame = rcpFrame;
		m_UniformBuffers[frameIndex]->SetData(&cb, sizeof(FxaaCB), 0);

		m_Sets[frameIndex]->SetTexture(kTexBinding, srcSampleView);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(FxaaCB)};
		m_Sets[frameIndex]->SetBuffer(kCbBinding, bb);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx->Draw(3, 1, 0);
	}
}
