#include "Renderer.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Platform/Vulkan/VulkanRendererAPI.hpp"

namespace Snowstorm
{
	Scope<RendererAPI> Renderer::s_API;
	std::vector<UniformRingBuffer> Renderer::s_FrameUniformRings;

	namespace
	{
		// Start simple; tune later (debug UI + stats will help).
		// This is per-frame-in-flight capacity, not total across all frames.
		constexpr uint32_t kDefaultFrameUniformRingSizeBytes = 4u * 1024u * 1024u; // 4 MB
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
	}

	void Renderer::Shutdown()
	{
		for (auto& ring : s_FrameUniformRings)
		{
			ring.Shutdown();
		}
		s_FrameUniformRings.clear();

		if (!s_API)
		{
			return;
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