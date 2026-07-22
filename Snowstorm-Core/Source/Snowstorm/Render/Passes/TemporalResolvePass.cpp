#include "TemporalResolvePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		// Must match ResolveCB in TemporalResolve.frag.hlsl (2 x 16-byte rows).
		struct ResolveCB
		{
			glm::vec2 RcpFrame{0.0f};
			float HistoryValid = 0.0f;
			float BlendHistory = 0.9f; // base weight while moving
			float MaxBlend = 0.97f;    // weight when ~static (velocity-ramped in-shader)
			glm::vec3 _Pad{0.0f};
		};

		// Set 1 (space1). Bindings parked high to dodge the material bindings (0/1/2) that
		// Fullscreen.vert.hlsl drags in via Engine.hlsli — same reasoning as FxaaPass.
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kCbBinding = 3;       // cbuffer ResolveCB : b3
		constexpr uint32_t kCurrentBinding = 4;  // Texture2D CurrentTex : t4
		constexpr uint32_t kHistoryBinding = 5;  // Texture2D HistoryTex : t5
		constexpr uint32_t kVelocityBinding = 6; // Texture2D VelocityTex : t6
		constexpr uint32_t kSamplerBinding = 7;  // SamplerState LinearClamp : s7
	}

	void TemporalResolvePass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Clamp-to-edge bilinear: history is sampled at a reprojected UV that can land between texels (the
		// whole point), and a tap near the edge must not wrap.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "TemporalResolveSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create TemporalResolve sampler");
	}

	void TemporalResolvePass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Fullscreen.vert.hlsl", "Engine/Shaders/TemporalResolve.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load TemporalResolve shader");

		if (!shader->IsReady())
		{
			return; // async compile; Draw null-guards and retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};       // full-res HDR history slot (RGBA16F)
		p.DepthFormat = PixelFormat::Unknown; // color-only history target
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.DebugName = "TemporalResolvePipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create TemporalResolve pipeline");
		m_ColorFormat = colorFormat;
	}

	void TemporalResolvePass::Draw(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                               const Ref<TextureView>& current, const Ref<TextureView>& history, const Ref<TextureView>& velocity,
	                               const glm::vec2& rcpFrame, const bool historyValid, const float blend, const float maxBlend,
	                               const PixelFormat colorFormat)
	{
		if (!ctx || !current || !history || !velocity)
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
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "TemporalResolve pipeline missing set=1 layout");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "TemporalResolve_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);

			m_UniformBuffers[frameIndex] = Buffer::Create(sizeof(ResolveCB), BufferUsage::Uniform, nullptr, true, "ResolveCB");
			m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		}

		ResolveCB cb{};
		cb.RcpFrame = rcpFrame;
		cb.HistoryValid = historyValid ? 1.0f : 0.0f;
		cb.BlendHistory = blend;
		cb.MaxBlend = maxBlend;
		m_UniformBuffers[frameIndex]->SetData(&cb, sizeof(ResolveCB), 0);

		// Source views change every frame (history ping-pongs, targets resize), so refresh all bindings.
		const BufferBinding bb{.Buffer = m_UniformBuffers[frameIndex], .Offset = 0, .Range = sizeof(ResolveCB)};
		m_Sets[frameIndex]->SetBuffer(kCbBinding, bb);
		m_Sets[frameIndex]->SetTexture(kCurrentBinding, current);
		m_Sets[frameIndex]->SetTexture(kHistoryBinding, history);
		m_Sets[frameIndex]->SetTexture(kVelocityBinding, velocity);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx->Draw(3, 1, 0);
	}
}
