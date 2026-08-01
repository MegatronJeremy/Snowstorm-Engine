#include "GIUpsamplePass.hpp"

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
		// Set-1 bindings in GIUpsample.frag.hlsl (parked high to dodge the shared fullscreen VS's material set).
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kGIBinding = 4;
		constexpr uint32_t kGBufferBinding = 5;
		constexpr uint32_t kSamplerBinding = 6;
		constexpr uint32_t kParamsBinding = 7;

		// Mirrors GIUpsampleCB in GIUpsample.frag.hlsl.
		struct GIUpsampleCB
		{
			glm::uvec2 GISize{0, 0};
			glm::uvec2 FullSize{0, 0};

			float Near = 0.1f;
			float Far = 500.0f;
			float DepthSigma = 50.0f; // relative view-depth edge-stop tightness (render.rt.depthsigma)
			float _Pad = 0.0f;
		};
	}

	void GIUpsamplePass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// #129 Inc 2c: NEAREST (point) filter — the guide must NOT be bilinear-blended across silhouettes (that
		// midpoint normal/depth fuzzes the bilateral rejection and smears GI over the edge). The half-res GI
		// itself is Load'd at integer texels, so it's unaffected by the sampler filter.
		SamplerDesc s{};
		s.MinFilter = Filter::Nearest;
		s.MagFilter = Filter::Nearest;
		s.MipmapMode = SamplerMipmapMode::Nearest;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "GIUpsamplePointSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create GI upsample sampler");
	}

	void GIUpsamplePass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Fullscreen.vert.hlsl", "Engine/Shaders/GIUpsample.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load GIUpsample shader");
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
		p.DebugName = "GIUpsamplePipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GIUpsample pipeline");
		m_ColorFormat = colorFormat;
	}

	void GIUpsamplePass::Draw(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Ref<TextureView>& gi,
	                          const Ref<TextureView>& gbuffer, const uint32_t giW, const uint32_t giH, const float nearPlane,
	                          const float farPlane, const float depthSigma, const PixelFormat colorFormat)
	{
		if (!ctx || !gi || !gbuffer)
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
			m_ParamBuffers[frameIndex] = Buffer::Create(sizeof(GIUpsampleCB), BufferUsage::Uniform, nullptr, true, "GIUpsampleCB");
		}
		if (!m_Sets[frameIndex])
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "GIUpsample pipeline missing set=1 layout");
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "GIUpsample_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);
		}

		GIUpsampleCB cb{};
		cb.GISize = {giW, giH};
		cb.FullSize = {0, 0}; // resolution-independent (UV-based); kept for parity with the shader layout
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.DepthSigma = depthSigma;
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(GIUpsampleCB), 0);

		m_Sets[frameIndex]->SetTexture(kGIBinding, gi);
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(GIUpsampleCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx->Draw(3, 1, 0);
	}
}
