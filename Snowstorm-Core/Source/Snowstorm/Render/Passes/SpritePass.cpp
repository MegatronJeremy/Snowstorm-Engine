#include "SpritePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <algorithm>
#include <bit>

namespace Snowstorm
{
	namespace
	{
		// Bindings in Sprite.vert/frag.hlsl, set 1.
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kInstanceBinding = 0;
		constexpr uint32_t kPointSamplerBinding = 1;
		constexpr uint32_t kLinearSamplerBinding = 2;

		// Mirrors SpritePush in Sprite.vert.hlsl.
		struct SpritePush
		{
			glm::mat4 ViewProjection;
			uint32_t BaseInstance;
		};

		Ref<Sampler> MakeSampler(const Filter filter, const char* name)
		{
			SamplerDesc s{};
			s.MinFilter = filter;
			s.MagFilter = filter;
			s.MipmapMode = filter == Filter::Nearest ? SamplerMipmapMode::Nearest : SamplerMipmapMode::Linear;
			s.AddressU = SamplerAddressMode::ClampToEdge;
			s.AddressV = SamplerAddressMode::ClampToEdge;
			s.AddressW = SamplerAddressMode::ClampToEdge;
			s.EnableAnisotropy = false;
			s.DebugName = name;
			return Sampler::Create(s);
		}
	}

	SpritePass::Batch SpritePass::Stage(const uint64_t frameCounter, const std::vector<SpriteInstance>& instances)
	{
		if (m_StagedFrame != frameCounter)
		{
			m_Staged.clear();
			m_StagedFrame = frameCounter;
		}
		const Batch batch{static_cast<uint32_t>(m_Staged.size()), static_cast<uint32_t>(instances.size())};
		m_Staged.insert(m_Staged.end(), instances.begin(), instances.end());
		return batch;
	}

	void SpritePass::EnsureSamplers()
	{
		if (!m_PointSampler)
		{
			m_PointSampler = MakeSampler(Filter::Nearest, "SpritePointSampler");
			m_LinearSampler = MakeSampler(Filter::Linear, "SpriteLinearSampler");
		}
	}

	void SpritePass::EnsurePipeline(const PixelFormat colorFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Sprite.vert.hlsl", "Engine/Shaders/Sprite.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Sprite shader");
		if (!shader->IsReady())
		{
			return; // async compile; Draw retries next frame
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown;
		p.Raster.Cull = CullMode::None; // a flip mirrors the quad and reverses its winding
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		// The present target is shown through ImGui, which blends by its alpha, so it must stay opaque.
		p.Blend.Attachments = {{.EnableBlend = true, .KeepDestinationAlpha = true}};
		p.PushConstants = {{.Offset = 0, .Size = sizeof(SpritePush), .Stages = ShaderStage::Vertex}};
		p.DebugName = "SpritePipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Sprite pipeline");
		m_ColorFormat = colorFormat;
		m_Sets.clear(); // built from the old pipeline's layouts
		m_UploadedFrame = UINT64_MAX;
	}

	void SpritePass::Upload(const uint32_t frameIndex)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		m_InstanceBuffers.resize(std::max<size_t>(m_InstanceBuffers.size(), frames));
		m_Sets.resize(std::max<size_t>(m_Sets.size(), frames));

		const size_t bytes = m_Staged.size() * sizeof(SpriteInstance);
		Ref<Buffer>& buffer = m_InstanceBuffers[frameIndex];
		if (!buffer || buffer->GetSize() < bytes)
		{
			const size_t capacity = std::bit_ceil(std::max<size_t>(bytes, 64 * sizeof(SpriteInstance)));
			buffer = Buffer::Create(capacity, BufferUsage::Storage, nullptr, true, "SpriteInstances");
			m_Sets[frameIndex].reset();
		}
		buffer->SetData(m_Staged.data(), bytes);

		if (!m_Sets[frameIndex])
		{
			const auto& layouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(layouts.size() > kSetIndex && layouts[kSetIndex], "Sprite pipeline missing set 1 layout");
			DescriptorSetDesc desc{};
			desc.DebugName = "Sprite_Set1";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[kSetIndex], desc);
			m_Sets[frameIndex]->SetBuffer(kInstanceBinding, {.Buffer = buffer});
			m_Sets[frameIndex]->SetSampler(kPointSamplerBinding, m_PointSampler);
			m_Sets[frameIndex]->SetSampler(kLinearSamplerBinding, m_LinearSampler);
			m_Sets[frameIndex]->Commit();
		}
	}

	void SpritePass::Draw(CommandContext& ctx, const uint32_t frameIndex, const uint64_t frameCounter,
	                      const glm::mat4& viewProjection, const Batch batch, const PixelFormat colorFormat)
	{
		EnsureSamplers();
		EnsurePipeline(colorFormat);
		if (!m_Pipeline || batch.Count == 0 || m_StagedFrame != frameCounter)
		{
			return;
		}

		if (m_UploadedFrame != frameCounter)
		{
			Upload(frameIndex);
			m_UploadedFrame = frameCounter;
		}

		const SpritePush push{viewProjection, batch.First};
		ctx.BindPipeline(m_Pipeline);
		ctx.BindDescriptorSet(m_Sets[frameIndex], kSetIndex);
		ctx.BindGlobalResources();
		ctx.PushConstants(&push, sizeof(push));
		ctx.Draw(6, batch.Count, 0);
	}
}
