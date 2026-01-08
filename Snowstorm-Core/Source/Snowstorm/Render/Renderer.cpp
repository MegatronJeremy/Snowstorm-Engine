#include "Renderer.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Platform/Vulkan/VulkanRendererAPI.hpp"

namespace Snowstorm
{
	// TODO move this stuff somewhere to renderer data maybe
	Scope<RendererAPI> Renderer::s_API;
	std::vector<UniformRingBuffer> Renderer::s_FrameUniformRings;

	struct RendererData {
		Ref<TextureView> WhiteTexture;
		Ref<TextureView> BlackTexture;
		Ref<TextureView> NormalTexture; // (0.5, 0.5, 1.0)

		// Shared UI resources
		Ref<DescriptorSetLayout> UITextureLayout;
		Ref<Sampler> UISampler;
	};

	namespace
	{
		// Start simple; tune later (debug UI + stats will help).
		// This is per-frame-in-flight capacity, not total across all frames.
		constexpr uint32_t kDefaultFrameUniformRingSizeBytes = 4u * 1024u * 1024u; // 4 MB

		RendererData* s_Data = nullptr;
	}

	void Renderer::Init(void* windowHandle)
	{
		SS_CORE_ASSERT(!s_API, "Renderer already initialized");

		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is not supported");
			return;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL is not supported by this build/config.");
			return;

		case RendererAPI::API::Vulkan:
			s_API = CreateScope<VulkanRendererAPI>();
			break;

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 not implemented yet.");
			return;
		}

		SS_CORE_ASSERT(s_API, "Failed to create RendererAPI implementation");
		s_API->Init(windowHandle);

		// Allocate per-frame uniform rings after backend init (so frames-in-flight is known)
		const uint32_t frames = s_API->GetFramesInFlight();
		SS_CORE_ASSERT(frames > 0, "RendererAPI returned 0 frames in flight");

		s_FrameUniformRings.clear();
		s_FrameUniformRings.resize(frames);

		for (uint32_t i = 0; i < frames; ++i)
		{
			s_FrameUniformRings[i].Init(kDefaultFrameUniformRingSizeBytes);
		}

		s_Data = new RendererData();
		// --- Create shared UI data
		// Define the Layout for a standard UI Texture (1 CombinedImageSampler at binding 0)
		// We use Set 0 here as this descriptor set is standalone for ImGui
		DescriptorSetLayoutDesc uiLayoutDesc;
		uiLayoutDesc.SetIndex = 0;
		uiLayoutDesc.DebugName = "ImGui_Texture_Layout";

		DescriptorBindingDesc binding;
		binding.Binding = 0;
		binding.Type = DescriptorType::CombinedImageSampler;
		binding.Visibility = ShaderStage::Fragment;
		binding.Count = 1;
		uiLayoutDesc.Bindings.push_back(binding);
		s_Data->UITextureLayout = DescriptorSetLayout::Create(uiLayoutDesc);
		s_Data->UISampler = Sampler::Create({});

		// --- Create a 1x1 Default White Texture
		uint32_t white = 0xFFFFFFFF;
		TextureDesc desc;
		desc.Width = 1;
		desc.Height = 1;
		desc.Format = PixelFormat::RGBA8_UNorm;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		desc.DebugName = "DefaultWhite";

		auto tex = Texture::Create(desc);
		tex->SetData(&white, sizeof(uint32_t));
		s_Data->WhiteTexture = TextureView::Create(tex, {});

		// Since it's the first texture created, its Bindless Index will be 0.
		SS_CORE_ASSERT(s_Data->WhiteTexture->GetGlobalBindlessIndex() == 0, "White texture must be index 0!");
	}

	void Renderer::Shutdown()
	{
		if (!s_API)
		{
			return;
		}

		for (auto& ring : s_FrameUniformRings)
		{
			ring.Shutdown();
		}
		s_FrameUniformRings.clear();

		// Add this to ensure the vector capacity is released and any internal Ref<>s are gone
		std::vector<UniformRingBuffer>().swap(s_FrameUniformRings); 

		if (s_Data)
		{
			// DROP EVERY REF EXPLICITLY
			s_Data->WhiteTexture.reset();
			s_Data->BlackTexture.reset();
			s_Data->NormalTexture.reset();
			s_Data->UITextureLayout.reset();
			s_Data->UISampler.reset();

			delete s_Data;
			s_Data = nullptr;
		}

		s_API->Shutdown();
		s_API.reset();
	}

	void Renderer::BeginFrame()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");

		// Reset the current frame ring before any draws
		{
			const uint32_t frameIndex = s_API->GetCurrentFrameIndex();
			SS_CORE_ASSERT(frameIndex < s_FrameUniformRings.size(), "Frame index out of range");
			s_FrameUniformRings[frameIndex].BeginFrame();
		}

		s_API->BeginFrame();
	}

	void Renderer::EndFrame()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		s_API->EndFrame();
	}

	uint32_t Renderer::GetCurrentFrameIndex()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetCurrentFrameIndex();
	}

	uint32_t Renderer::GetFramesInFlight()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetFramesInFlight();
	}

	PixelFormat Renderer::GetSurfaceFormat()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetSurfaceFormat();
	}

	Ref<RenderTarget> Renderer::GetSwapchainTarget()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetSwapchainTarget();
	}

	UniformRingBuffer& Renderer::GetFrameUniformRing()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		const uint32_t frameIndex = s_API->GetCurrentFrameIndex();
		SS_CORE_ASSERT(frameIndex < s_FrameUniformRings.size(), "Frame index out of range");
		return s_FrameUniformRings[frameIndex];
	}

	uint32_t Renderer::GetMinUniformBufferOffsetAlignment()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetMinUniformBufferOffsetAlignment();
	}

	Ref<CommandContext> Renderer::GetGraphicsCommandContext()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->GetGraphicsCommandContext();
	}

	Ref<DescriptorSetLayout> Renderer::GetUITextureLayout()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_Data->UITextureLayout;
	}

	Ref<Sampler> Renderer::GetUISampler()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_Data->UISampler;
	}

	void Renderer::InitImGuiBackend(void* windowHandle)
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->InitImGuiBackend(windowHandle);
	}

	void Renderer::ShutdownImGuiBackend()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->ShutdownImGuiBackend();
	}

	void Renderer::ImGuiNewFrame()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->ImGuiNewFrame();
	}

	void Renderer::RenderImGuiDrawData(CommandContext& context)
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return s_API->RenderImGuiDrawData(context);
	}

	RendererAPI& Renderer::GetAPI()
	{
		SS_CORE_ASSERT(s_API, "Renderer not initialized");
		return *s_API;
	}
}