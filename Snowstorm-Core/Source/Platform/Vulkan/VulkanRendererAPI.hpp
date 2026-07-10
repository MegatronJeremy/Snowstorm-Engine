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

		void WaitIdle() override;

		bool BeginFrame() override;
		void EndFrame() override;

		float GetLastGpuWaitMs() const override { return m_LastGpuWaitMs; }
		float GetLastGpuFrameMs() const override { return m_LastGpuFrameMs; }

		void SetVSync(bool enabled) override;
		bool IsVSync() const override;

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
		// Wrap the context's current swapchain VkImages in Ref<VulkanTexture>. Called at init and
		// after every swapchain recreate so m_SwapchainTextures tracks the live images.
		void WrapSwapchainTextures();

		// Drain GPU, recreate the swapchain, and rewrap textures. Returns false when the swapchain
		// could not be created (minimized window) so the caller skips the frame.
		bool RecreateSwapchain();

		// (Re)create the per-swapchain-image render-finished / present-signal semaphores, sized to the live
		// image count. Called at init and after each swapchain recreate (image count can change with the
		// present mode). Destroys any existing ones first; the GPU is drained by the caller at both sites.
		void CreateRenderFinishedSemaphores();

		uint32_t m_CurrentFrameIndex = 0;
		uint32_t m_ImageIndex = 0; // The actual index of the swapchain image acquired

		float m_LastGpuWaitMs = 0.0f;  // time spent blocking on the in-flight fence in BeginFrame
		float m_LastGpuFrameMs = 0.0f; // GPU execution time of the last completed frame (timestamp query)

		// One timestamp query pool per frame-in-flight: write a start stamp at command-buffer Begin and
		// an end stamp before End, then read the previous frame's resolved pair (it has finished by the
		// time we reuse its slot). Disabled (pool == NULL) when the device reports no timestamp support.
		std::vector<VkQueryPool> m_TimestampPools;
		std::vector<bool> m_TimestampWritten; // slot has a resolvable pair from its previous use
		float m_TimestampPeriodNs = 0.0f;     // ns per timestamp tick (VkPhysicalDeviceLimits::timestampPeriod)
		bool m_TimestampsSupported = false;

		std::vector<Ref<CommandContext>> m_GraphicsContexts;
		std::vector<Ref<Texture>> m_SwapchainTextures;

		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
		std::vector<VkFence> m_InFlightFences;
	};
}
