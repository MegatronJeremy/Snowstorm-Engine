#pragma once

#include "Platform/Vulkan/VulkanCommandContext.hpp"
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

		std::string GetDeviceName() const override;

		bool IsRayTracingSupported() const override;
		bool IsOpacityMicromapSupported() const override;
		const std::vector<std::string>& GetGpuNames() const override;
		int GetSelectedGpuIndex() const override;
		bool IsFloat16Supported() const override;
		uint32_t GetMaxSampleCount() const override;

		Ref<CommandContext> GetGraphicsCommandContext() override;

		bool IsAsyncComputeAvailable() const override;
		Ref<CommandContext> ForkAsyncCompute() override;
		void JoinAsyncCompute() override;
		const std::vector<GpuScope>& GetCollectedGpuScopes() const override { return m_FrameGpuScopes; }

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

		// Resolve a freshly-begun context's prior recording into m_FrameGpuScopes. Must run while `ctx` is
		// recording: CollectGpuScopes issues a vkCmdResetQueryPool into it.
		void AppendCollectedScopes(VulkanCommandContext& ctx);

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

		// Graphics command buffers, [frame-in-flight][segment]. A frame with no async compute uses exactly one
		// segment and submits exactly as it always did; each fork/join pair closes the open segment and opens
		// another, so N forks produce N+1 segments. Segments are allocated lazily and reused across frames.
		std::vector<std::vector<Ref<VulkanCommandContext>>> m_GraphicsContexts;
		// Async-compute command buffers, [frame-in-flight][batch]. One per fork/join pair, same lazy reuse.
		std::vector<std::vector<Ref<VulkanCommandContext>>> m_ComputeContexts;

		// A submission recorded this frame, with the timeline values that order it against the other queue.
		// 0 means "no timeline wait/signal" (the first graphics segment waits on image-acquire instead, and
		// the last signals the present semaphore).
		struct QueuedSubmit
		{
			Ref<VulkanCommandContext> Ctx;
			uint64_t WaitTimeline = 0;
			uint64_t SignalTimeline = 0;
		};
		// Built during recording, drained in EndFrame. Submitted in timeline order: graphics[0], compute[0],
		// graphics[1], ... so a wait is never enqueued before the submit that will signal it.
		std::vector<QueuedSubmit> m_FrameGraphicsSubmits;
		std::vector<QueuedSubmit> m_FrameComputeSubmits;

		// Scopes resolved by every context begun this frame, in begin order. A context resolves its own pool
		// when it starts recording, so async passes land here interleaved with the graphics segments that
		// bracket them; their wall-clock execution overlaps, so these millisecond figures do not sum to the
		// frame time once a pass actually runs async.
		std::vector<GpuScope> m_FrameGpuScopes;

		uint32_t m_CurrentGraphicsSegment = 0; // index into m_GraphicsContexts[frame] currently recording
		uint32_t m_CurrentComputeBatch = 0;    // index into m_ComputeContexts[frame] for the next fork
		bool m_AsyncBatchOpen = false;         // between ForkAsyncCompute and JoinAsyncCompute

		// One timeline semaphore for the whole renderer, with a monotonically increasing value. Each fork
		// consumes two values (fork = graphics signals / compute waits; join = compute signals / graphics
		// waits). A single timeline replaces what would otherwise be a pair of binary semaphores per edge.
		// VK_NULL_HANDLE when the device has no dedicated compute queue (async compute then never engages).
		VkSemaphore m_Timeline = VK_NULL_HANDLE;
		uint64_t m_TimelineNext = 0; // last value handed out; ++ before each use

		std::vector<Ref<Texture>> m_SwapchainTextures;

		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
		std::vector<VkFence> m_InFlightFences;
	};
}
