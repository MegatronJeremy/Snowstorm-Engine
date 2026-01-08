#pragma once

#include "Platform/Vulkan/VulkanContext.hpp"

#include "Snowstorm/Render/RendererAPI.hpp"

namespace Snowstorm
{
	class VulkanRendererAPI final : public RendererAPI
	{
	public:
		void Init(void* windowHandle) override;
		void Shutdown() override;

		void BeginFrame() override;
		void EndFrame() override;

		uint32_t GetCurrentFrameIndex() const override;
		uint32_t GetFramesInFlight() const override;

		PixelFormat GetSurfaceFormat() const override;

		Ref<RenderTarget> GetSwapchainTarget() const override;

		uint32_t GetMinUniformBufferOffsetAlignment() const override;

		Ref<CommandContext> GetGraphicsCommandContext() override;

		void InitImGuiBackend(void* windowHandle) override;
		void ShutdownImGuiBackend() override;
		void ImGuiNewFrame() override;
		void RenderImGuiDrawData(CommandContext& context) override;

	private:
		uint32_t m_CurrentFrameIndex = 0;
		uint32_t m_ImageIndex = 0; // The actual index of the swapchain image acquired

		std::vector<Ref<CommandContext>> m_GraphicsContexts;
		std::vector<Ref<Texture>> m_SwapchainTextures;

		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
		std::vector<VkFence> m_InFlightFences;
	};
}
