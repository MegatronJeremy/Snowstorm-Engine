#include "SpritePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <algorithm>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kSetIndex = 1;
		constexpr uint32_t kConstantsBinding = 3; // cbuffer SpriteCB : b3
		constexpr uint32_t kInstancesBinding = 4; // StructuredBuffer<SpriteInstance> Sprites : t4
		constexpr uint32_t kSamplerBinding = 5;   // SamplerState SpriteSampler : s5
		constexpr uint32_t kVerticesPerSprite = 6;
		constexpr size_t kInitialCapacity = 64;
	}

	void SpritePass::EnsureSampler()
	{
		if (m_Sampler)
		{
			return;
		}
		// Clamp: a sprite's UV rect may sit on an atlas edge, and a repeat tap there would bleed the
		// neighbouring cell in.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "SpriteSampler";
		m_Sampler = Sampler::Create(s);
		SS_CORE_ASSERT(m_Sampler, "Failed to create Sprite sampler");
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
			return; // async compile; Draw null-guards and retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown;
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		p.Blend.Attachments = {PipelineBlendAttachment{.EnableBlend = true}};
		p.DebugName = "SpritePipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Sprite pipeline");
		m_ColorFormat = colorFormat;
	}

	SpritePass::FrameResources& SpritePass::EnsureFrameResources(const uint32_t frameIndex, const size_t spriteCount)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Frames.size() < frames)
		{
			m_Frames.resize(frames);
		}
		FrameResources& fr = m_Frames[frameIndex];

		if (!fr.Set)
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > kSetIndex && setLayouts[kSetIndex], "Sprite pipeline missing set=1 layout");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Sprite_Set1";
			fr.Set = DescriptorSet::Create(setLayouts[kSetIndex], setDesc);
			fr.Constants = Buffer::Create(sizeof(SpriteCanvasConstants), BufferUsage::Uniform, nullptr, true, "SpriteCB");
		}

		if (fr.Capacity < spriteCount)
		{
			size_t capacity = std::max(fr.Capacity, kInitialCapacity);
			while (capacity < spriteCount)
			{
				capacity *= 2;
			}
			// The frame slot's previous buffer may still be read by the GPU for this slot's last submission;
			// the Ref released here is destroyed by the backend's deferred-release path, as any buffer is.
			fr.Instances = Buffer::Create(capacity * sizeof(SpriteInstance), BufferUsage::Storage, nullptr, true, "SpriteInstances");
			fr.Capacity = capacity;
		}
		return fr;
	}

	void SpritePass::Draw(CommandContext& ctx, const uint32_t frameIndex, const std::vector<SpriteInstance>& sprites,
	                      const SpriteCanvasConstants& canvas, const PixelFormat colorFormat)
	{
		if (sprites.empty() || canvas.CanvasScale <= 0.0f)
		{
			return;
		}

		EnsureSampler();
		EnsurePipeline(colorFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		FrameResources& fr = EnsureFrameResources(frameIndex, sprites.size());
		fr.Constants->SetData(&canvas, sizeof(SpriteCanvasConstants), 0);
		fr.Instances->SetData(sprites.data(), sprites.size() * sizeof(SpriteInstance), 0);

		fr.Set->SetBuffer(kConstantsBinding, {.Buffer = fr.Constants, .Offset = 0, .Range = sizeof(SpriteCanvasConstants)});
		fr.Set->SetBuffer(kInstancesBinding, {.Buffer = fr.Instances, .Offset = 0, .Range = 0});
		fr.Set->SetSampler(kSamplerBinding, m_Sampler);
		fr.Set->Commit();

		ctx.BindPipeline(m_Pipeline);
		ctx.BindDescriptorSet(fr.Set, kSetIndex);
		ctx.BindGlobalResources(); // set 3: the bindless textures the fragment stage indexes
		ctx.Draw(kVerticesPerSprite, static_cast<uint32_t>(sprites.size()), 0);
	}
}
