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
		// corrupts the AO reconstruction, so keep in lockstep with the shader.
		struct AOCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::uvec2 OutSize{0, 0};
			float AORadius = 0.5f;
			float AOIntensity = 1.0f;
			uint32_t FrameCounter = 0;
			uint32_t RayCount = 2;           // render.ao.rays (clamped): occlusion rays/pixel this frame
			uint32_t ReflGeoTableAddrLo = 0; // geometry-table device address (lo/hi) for the cutout alpha test
			uint32_t ReflGeoTableAddrHi = 0;
		};

		// Binding indices in AO.comp.hlsl set 0.
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		constexpr uint32_t kSamplerBinding = 2; // wrapping sampler for the cutout alpha lookup (was unused #129 2c)
		constexpr uint32_t kParamsBinding = 3;
		constexpr uint32_t kDepthBinding = 4; // fp32 D32 depth SRV (was packed in the G-buffer .w)
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

		// Wrapping linear sampler for the bindless albedo ALPHA lookup in the cutout any-hit test (foliage
		// textures tile, so Repeat, matching the raster material sampler). The G-buffer itself is still
		// point-fetched via Load; this sampler is only for the alpha test.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::Repeat;
		s.AddressV = SamplerAddressMode::Repeat;
		s.AddressW = SamplerAddressMode::Repeat;
		s.EnableAnisotropy = false;
		s.DebugName = "AOSampler";
		m_Sampler = Sampler::Create(s);

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
	                      const uint32_t rayCount, const uint64_t tableAddr, const Ref<TextureView>& gbuffer,
	                      const Ref<TextureView>& depth, const Ref<TextureView>& output, const uint32_t outW,
	                      const uint32_t outH)
	{
		if (!ctx || !gbuffer || !depth || !output || outW == 0 || outH == 0)
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
		cb.RayCount = rayCount;
		cb.ReflGeoTableAddrLo = static_cast<uint32_t>(tableAddr & 0xFFFFFFFFull);
		cb.ReflGeoTableAddrHi = static_cast<uint32_t>(tableAddr >> 32);
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(AOCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "AO pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "AOSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);   // .xy normal, .z roughness
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);       // fp32 D32 depth SRV
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);     // storage image (UAV)
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler); // cutout alpha lookup
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(AOCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions graph-managed (#129 Inc 4): the effect declares this output in .Writes (-> Storage);
		// AOUpsample's .Reads does the read-back to Sampled. No hand-called transitions.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless Textures[] + SceneTLAS (written by TlasBuildSystem)
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
