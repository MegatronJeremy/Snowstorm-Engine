#include "VulkanRendererAPI.hpp"

#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "VulkanBindlessManager.hpp"
#include "VulkanOmmBaker.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanTexture.hpp"
#include "Platform/Vulkan/VulkanCommandContext.hpp"

#include <chrono>
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Windows/WindowsWindow.hpp"

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t s_MaxFramesInFlight = 2;

		VkDescriptorPool s_ImGuiPool = VK_NULL_HANDLE;
	}

	void VulkanRendererAPI::Init(void* windowHandle)
	{
		VulkanContext::Get().Init(windowHandle);

		const auto& context = VulkanContext::Get();
		const VkDevice device = context.GetDevice();

		WrapSwapchainTextures();

		m_CurrentFrameIndex = 0;

		m_GraphicsContexts.resize(s_MaxFramesInFlight);
		m_ComputeContexts.resize(s_MaxFramesInFlight);
		m_ImageAvailableSemaphores.resize(s_MaxFramesInFlight);
		m_InFlightFences.resize(s_MaxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so we don't wait forever on frame 0

		// The image-acquire semaphore is per-frame-in-flight (you need a free one BEFORE the acquire tells
		// you which image you got). The fence is per-frame-in-flight too (throttles CPU to N frames ahead).
		for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
		{
			// Segment 0 always exists; further segments (and any async-compute batches) are allocated on
			// demand by ForkAsyncCompute and then reused for the life of the renderer.
			m_GraphicsContexts[i].push_back(CreateRef<VulkanCommandContext>(GpuQueue::Graphics));

			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]);
			vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]);
		}

		// Only meaningful with a dedicated compute family: without one async compute never engages and the
		// semaphore is never waited on. Its null-ness is the capability check in IsAsyncComputeAvailable.
		if (context.HasDedicatedComputeQueue())
		{
			VkSemaphoreTypeCreateInfo typeInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
			typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
			typeInfo.initialValue = 0;

			VkSemaphoreCreateInfo timelineInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			timelineInfo.pNext = &typeInfo;
			if (vkCreateSemaphore(device, &timelineInfo, nullptr, &m_Timeline) != VK_SUCCESS)
			{
				SS_CORE_WARN("Failed to create the async-compute timeline semaphore; async compute disabled.");
				m_Timeline = VK_NULL_HANDLE;
			}
		}

		// The render-finished / present-signal semaphore is PER-SWAPCHAIN-IMAGE (indexed by image index),
		// NOT per-frame-in-flight: a present-wait semaphore can only be reused once its image is re-acquired
		// (Vulkan spec), which is keyed to the image, not the frame slot. With 3 swapchain images but 2
		// frames-in-flight, a per-frame array reuses a semaphore while a prior present on another image is
		// still pending -> "semaphore signaled but may still be in use by VkSwapchainKHR". Sized to the
		// actual image count + rebuilt on swapchain recreate (image count can change with present mode).
		CreateRenderFinishedSemaphores();

		// GPU frame timing: a 2-query timestamp pool per frame-in-flight (start + end). Requires the
		// device to support timestamps on the graphics queue (timestampPeriod != 0 and the queue family
		// allows them). If unsupported we leave the pools null and report 0 ms — no functional impact.
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(context.GetPhysicalDevice(), &props);
		m_TimestampPeriodNs = props.limits.timestampPeriod;
		m_TimestampsSupported = m_TimestampPeriodNs > 0.0f;
		m_TimestampPools.resize(s_MaxFramesInFlight, VK_NULL_HANDLE);
		m_TimestampWritten.resize(s_MaxFramesInFlight, false);
		if (m_TimestampsSupported)
		{
			VkQueryPoolCreateInfo qpInfo{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
			qpInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			qpInfo.queryCount = 2;
			for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
			{
				if (vkCreateQueryPool(device, &qpInfo, nullptr, &m_TimestampPools[i]) != VK_SUCCESS)
				{
					SS_CORE_WARN("Failed to create timestamp query pool; GPU frame timing disabled.");
					m_TimestampsSupported = false;
					break;
				}
			}
		}
		else
		{
			SS_CORE_WARN("Device reports no timestamp support; GPU frame timing disabled.");
		}
	}

	void VulkanRendererAPI::WrapSwapchainTextures()
	{
		const auto& context = VulkanContext::Get();
		const VkDevice device = context.GetDevice();

		const auto& swapImages = context.GetSwapchainImages();
		m_SwapchainTextures.clear();
		m_SwapchainTextures.resize(swapImages.size());

		TextureDesc desc;
		desc.Width = context.GetSwapchainExtent().width;
		desc.Height = context.GetSwapchainExtent().height;
		desc.Format = FromVkFormat(context.GetSwapchainFormat());
		desc.Usage = TextureUsage::ColorAttachment;

		for (uint32_t i = 0; i < swapImages.size(); ++i)
		{
			desc.DebugName = "Swapchain[" + std::to_string(i) + "]";
			m_SwapchainTextures[i] = CreateRef<VulkanTexture>(swapImages[i], desc);
			SetVulkanObjectName(device, reinterpret_cast<uint64_t>(swapImages[i]),
			                    VK_OBJECT_TYPE_IMAGE, desc.DebugName.c_str());
		}
	}

	void VulkanRendererAPI::CreateRenderFinishedSemaphores()
	{
		const VkDevice device = VulkanContext::Get().GetDevice();

		// Destroy any existing (recreate path) — the GPU is drained by the caller (init: nothing pending;
		// RecreateSwapchain: VulkanContext drains before this), so none are in use.
		for (const VkSemaphore s : m_RenderFinishedSemaphores)
		{
			if (s != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, s, nullptr);
			}
		}

		const uint32_t imageCount = static_cast<uint32_t>(VulkanContext::Get().GetSwapchainImages().size());
		m_RenderFinishedSemaphores.assign(imageCount, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
		}
	}

	bool VulkanRendererAPI::RecreateSwapchain()
	{
		// VulkanContext::RecreateSwapchain drains the GPU before tearing down the old images, so the
		// Ref<VulkanTexture> wrappers we still hold are safe to drop and rebuild here.
		if (!VulkanContext::Get().RecreateSwapchain())
		{
			return false; // zero-extent surface (minimized) — skip the frame
		}
		WrapSwapchainTextures();
		// Image count can change with the present mode (e.g. mailbox vs FIFO), so rebuild the per-image
		// render-finished semaphores to match. Safe: the context drained the GPU during recreate.
		CreateRenderFinishedSemaphores();
		return true;
	}

	void VulkanRendererAPI::WaitIdle()
	{
		vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
	}

	void VulkanRendererAPI::SetVSync(const bool enabled)
	{
		auto& context = VulkanContext::Get();
		if (context.IsVSync() == enabled)
		{
			return;
		}
		context.SetVSync(enabled); // store desired present mode (no recreate here)
		RecreateSwapchain();       // RHI path: recreate + re-wrap swapchain textures
		SS_CORE_INFO("VSync {}", enabled ? "on (FIFO)" : "off (uncapped)");
	}

	bool VulkanRendererAPI::IsVSync() const
	{
		return VulkanContext::Get().IsVSync();
	}

	void VulkanRendererAPI::Shutdown()
	{
		const VkDevice device = VulkanContext::Get().GetDevice();

		// Wait for GPU to finish work before destroying the core context
		vkDeviceWaitIdle(device);

		VulkanBindlessManager::Get().Shutdown();

		// Function-local-static singleton owning the OMM bake pipeline + sampler; release here (device still alive)
		// so its Refs don't destruct at process exit on a dead device. No-op when OMM never baked (m_Pipeline null).
		VulkanOmmBaker::Get().Shutdown();

		// You must clear all swapchain textures we wrapped
		// These hold Ref<VulkanTexture> which own VMA allocations
		m_SwapchainTextures.clear();

		for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
		{
			vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
			vkDestroyFence(device, m_InFlightFences[i], nullptr);
			if (m_TimestampPools[i] != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(device, m_TimestampPools[i], nullptr);
			}
		}

		// Render-finished semaphores are per-swapchain-image (not per-frame-in-flight), so destroy them
		// over their own (image-count-sized) array.
		for (const VkSemaphore s : m_RenderFinishedSemaphores)
		{
			if (s != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, s, nullptr);
			}
		}
		m_RenderFinishedSemaphores.clear();

		if (m_Timeline != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(device, m_Timeline, nullptr);
			m_Timeline = VK_NULL_HANDLE;
		}

		// Both context pools free their command buffers back to their own queue family's pool (which the
		// VulkanContext::Shutdown below then destroys), so they must be released first.
		m_FrameGraphicsSubmits.clear();
		m_FrameComputeSubmits.clear();
		m_GraphicsContexts.clear();
		m_ComputeContexts.clear();

		// 2. Shut down the low-level context (Allocator, Device, Instance)
		VulkanContext::Get().Shutdown();
	}

	bool VulkanRendererAPI::BeginFrame()
	{
		auto& context = VulkanContext::Get();
		VkDevice device = context.GetDevice();

		// GPU/present wait: time BOTH the in-flight fence wait AND the swapchain image acquire. Under
		// vsync (FIFO) the throttle shows up in vkAcquireNextImageKHR, not the fence, so timing only
		// the fence misses it. This is a stall, not CPU work — surfaced as "GPU wait" so the editor
		// overlay doesn't misread it as render cost. (Measured: stress scene renders in ~0.5ms of real
		// work; the rest of the frame is this acquire stall waiting on the 60 Hz present queue.)
		const auto waitStart = std::chrono::steady_clock::now();

		// 1. Wait for the GPU to finish the frame we are about to reuse. (Fence reset is deferred
		// until AFTER a successful acquire — resetting here and then bailing on OUT_OF_DATE would
		// leave the fence unsignaled forever, hanging the next wait on this slot.)
		vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrameIndex], VK_TRUE, UINT64_MAX);

		// 2. Acquire an image from the swapchain. On OUT_OF_DATE (surface changed, e.g. resize) the
		// swapchain is unusable: rebuild it and retry. SUBOPTIMAL still works for this frame; we
		// rebuild after present instead. Note: m_ImageIndex may differ from m_CurrentFrameIndex.
		VkResult result = vkAcquireNextImageKHR(
		    device,
		    context.GetSwapchain(),
		    UINT64_MAX,
		    m_ImageAvailableSemaphores[m_CurrentFrameIndex],
		    VK_NULL_HANDLE,
		    &m_ImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			if (!RecreateSwapchain())
			{
				return false; // minimized / zero-extent: skip the frame
			}
			result = vkAcquireNextImageKHR(
			    device,
			    context.GetSwapchain(),
			    UINT64_MAX,
			    m_ImageAvailableSemaphores[m_CurrentFrameIndex],
			    VK_NULL_HANDLE,
			    &m_ImageIndex);
		}

		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			SS_CORE_ERROR("vkAcquireNextImageKHR failed: {0}", static_cast<int>(result));
			return false;
		}

		m_LastGpuWaitMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - waitStart).count();

		// 3. Acquire succeeded — now it's safe to reset the fence and begin recording.
		vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrameIndex]);

		if (const int dumpFrames = CVars::MemoryDump.Get(); dumpFrames > 0)
		{
			context.LogMemoryStats();
			CVars::MemoryDump.Set(dumpFrames - 1);
		}
		// Reset the per-frame recording state: every frame starts on graphics segment 0 with no async batch
		// open and no submissions queued. Segments/batches allocated by earlier frames stay in the pool.
		m_CurrentGraphicsSegment = 0;
		m_CurrentComputeBatch = 0;
		m_AsyncBatchOpen = false;
		m_FrameGraphicsSubmits.clear();
		m_FrameComputeSubmits.clear();

		const Ref<VulkanCommandContext> ctx = m_GraphicsContexts[m_CurrentFrameIndex][0];
		ctx->Begin();

		m_FrameGpuScopes.clear();
		AppendCollectedScopes(*ctx);

		// A frame that never forks ends with exactly this one entry, which EndFrame finalizes with the
		// present signal and fence, reproducing the original single submit.
		m_FrameGraphicsSubmits.push_back({.Ctx = ctx});

		// GPU frame timing. We waited on this slot's fence above, so its prior submission is complete and
		// its timestamps are resolvable; read them, then reset the pool and write a fresh start stamp.
		if (m_TimestampsSupported)
		{
			const VkQueryPool pool = m_TimestampPools[m_CurrentFrameIndex];
			if (m_TimestampWritten[m_CurrentFrameIndex])
			{
				uint64_t stamps[2] = {0, 0};
				if (vkGetQueryPoolResults(device, pool, 0, 2, sizeof(stamps), stamps, sizeof(uint64_t),
				                          VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
				{
					m_LastGpuFrameMs = static_cast<float>(stamps[1] - stamps[0]) * m_TimestampPeriodNs * 1e-6f;
				}
			}
			vkCmdResetQueryPool(ctx->GetVulkanCommandBuffer(), pool, 0, 2);
			vkCmdWriteTimestamp(ctx->GetVulkanCommandBuffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
		}
		return true;
	}

	void VulkanRendererAPI::EndFrame()
	{
		auto& context = VulkanContext::Get();

		// An unjoined batch would leave the compute buffer open and its join value never signalled, hanging
		// the final graphics submit. RenderGraph always pairs them; assert so a future caller can't not.
		SS_CORE_ASSERT(!m_AsyncBatchOpen, "EndFrame with an async-compute batch still open");

		// The present transition and the frame's end timestamp belong to the last graphics segment, the one
		// still recording. Without a fork that is segment 0.
		const Ref<VulkanCommandContext> ctx = m_GraphicsContexts[m_CurrentFrameIndex][m_CurrentGraphicsSegment];

		// 1. Transition to a presentable state
		ctx->TransitionLayout(m_SwapchainTextures[m_ImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		// GPU frame timing: end stamp after all work is recorded (bottom of pipe = everything done).
		if (m_TimestampsSupported)
		{
			vkCmdWriteTimestamp(ctx->GetVulkanCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			                    m_TimestampPools[m_CurrentFrameIndex], 1);
			m_TimestampWritten[m_CurrentFrameIndex] = true;
		}

		// 2. End command recording
		ctx->End();

		// 3. Submit to Queue (synchronization2). Using vkQueueSubmit2 so the image-acquire wait is
		// a VkSemaphoreSubmitInfo with an explicit stage mask, which forms a proper execution
		// dependency with the swapchain image's first sync2 layout transition (in BeginRenderPass).
		// A sync1 vkQueueSubmit wait does not chain into a vkCmdPipelineBarrier2, so validation
		// reports "semaphore signaled by image acquire was not waited on".
		//
		// With async compute the frame is a chain of submits rather than one: graphics[0], compute[0],
		// graphics[1] and so on, ordered by the timeline values assigned at fork/join. They are issued in
		// that same order so a wait is never enqueued before the submit that will signal it. The first
		// graphics segment waits on image-acquire; the last signals the present semaphore and carries the
		// in-flight fence, which therefore covers the compute batches too since that segment waits on the
		// final join. A frame with no fork has one graphics submit and no compute ones.
		auto submitOne = [&](const QueuedSubmit& sub, const bool isFirstGraphics, const bool isLastGraphics,
		                     const VkQueue queue, const VkFence fence)
		{
			VkSemaphoreSubmitInfo waits[1]{};
			VkSemaphoreSubmitInfo signals[1]{};
			uint32_t waitCount = 0;
			uint32_t signalCount = 0;

			if (isFirstGraphics)
			{
				waits[waitCount++] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				                      .semaphore = m_ImageAvailableSemaphores[m_CurrentFrameIndex],
				                      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
			}
			else if (sub.WaitTimeline != 0)
			{
				waits[waitCount++] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				                      .semaphore = m_Timeline,
				                      .value = sub.WaitTimeline,
				                      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
			}

			if (isLastGraphics)
			{
				// Present-signal semaphore is indexed by image, not frame-in-flight (see
				// CreateRenderFinishedSemaphores).
				signals[signalCount++] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				                          .semaphore = m_RenderFinishedSemaphores[m_ImageIndex],
				                          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
			}
			else if (sub.SignalTimeline != 0)
			{
				signals[signalCount++] = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				                          .semaphore = m_Timeline,
				                          .value = sub.SignalTimeline,
				                          .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
			}

			VkCommandBufferSubmitInfo cmdInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
			cmdInfo.commandBuffer = sub.Ctx->GetVulkanCommandBuffer();

			VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
			submitInfo.waitSemaphoreInfoCount = waitCount;
			submitInfo.pWaitSemaphoreInfos = waits;
			submitInfo.commandBufferInfoCount = 1;
			submitInfo.pCommandBufferInfos = &cmdInfo;
			submitInfo.signalSemaphoreInfoCount = signalCount;
			submitInfo.pSignalSemaphoreInfos = signals;

			if (vkQueueSubmit2(queue, 1, &submitInfo, fence) == VK_ERROR_DEVICE_LOST)
			{
				// A submit faulted the GPU. Dump VK_EXT_device_fault detail (faulting addresses / vendor
				// description) before anything downstream turns the sticky device-lost into a bare -4 at the
				// next acquire/submit. No-op unless the extension is enabled (Debug). A shader OOB (e.g. an
				// out-of-range geometry-table read) surfaces here first.
				context.LogDeviceFaultInfo();
			}
		};

		const size_t graphicsCount = m_FrameGraphicsSubmits.size();
		for (size_t i = 0; i < graphicsCount; ++i)
		{
			const bool isLast = i + 1 == graphicsCount;
			submitOne(m_FrameGraphicsSubmits[i], i == 0, isLast, context.GetGraphicsQueue(),
			          isLast ? m_InFlightFences[m_CurrentFrameIndex] : VK_NULL_HANDLE);

			// The compute batch forked from this segment goes next, so the chain is issued in timeline order.
			if (i < m_FrameComputeSubmits.size())
			{
				submitOne(m_FrameComputeSubmits[i], false, false, context.GetComputeQueue(), VK_NULL_HANDLE);
			}
		}

		VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_ImageIndex]};

		// 4. Present
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapchains[] = {context.GetSwapchain()};
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &m_ImageIndex;

		// OUT_OF_DATE/SUBOPTIMAL here means the surface changed during the frame (resize). Rebuild
		// the swapchain so the next acquire starts clean. The rebuild drains the GPU, so the work we
		// just submitted is complete before the old images are destroyed.
		if (const VkResult presentResult = vkQueuePresentKHR(context.GetGraphicsQueue(), &presentInfo);
		    presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
		{
			RecreateSwapchain();
		}

		// 5. Advance frame counter
		m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % s_MaxFramesInFlight;
	}

	uint32_t VulkanRendererAPI::GetCurrentFrameIndex() const
	{
		return m_CurrentFrameIndex;
	}

	uint32_t VulkanRendererAPI::GetFramesInFlight() const
	{
		return s_MaxFramesInFlight;
	}

	PixelFormat VulkanRendererAPI::GetSurfaceFormat() const
	{
		return FromVkFormat(VulkanContext::Get().GetSwapchainFormat());
	}

	Ref<RenderTarget> VulkanRendererAPI::GetSwapchainTarget() const
	{
		auto& context = VulkanContext::Get();

		RenderTargetDesc desc;
		desc.Width = context.GetSwapchainExtent().width;
		desc.Height = context.GetSwapchainExtent().height;
		desc.IsSwapchainTarget = true;

		RenderTargetAttachment color;
		color.View = m_SwapchainTextures[m_ImageIndex]->GetDefaultView();
		color.LoadOp = RenderTargetLoadOp::Clear;
		color.StoreOp = RenderTargetStoreOp::Store;
		color.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
		desc.ColorAttachments.push_back(color);

		return RenderTarget::Create(desc);
	}

	uint32_t VulkanRendererAPI::GetMinUniformBufferOffsetAlignment() const
	{
		const VkPhysicalDevice physDevice = VulkanContext::Get().GetPhysicalDevice();
		SS_CORE_ASSERT(physDevice != VK_NULL_HANDLE, "VulkanRendererAPI: PhysicalDevice is null");

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physDevice, &props);

		const VkDeviceSize a = props.limits.minUniformBufferOffsetAlignment;
		SS_CORE_ASSERT(a > 0, "VulkanRendererAPI: minUniformBufferOffsetAlignment is 0");
		SS_CORE_ASSERT(a <= 0xffffffffull, "VulkanRendererAPI: alignment doesn't fit in uint32_t");

		return static_cast<uint32_t>(a);
	}

	std::string VulkanRendererAPI::GetDeviceName() const
	{
		const VkPhysicalDevice physDevice = VulkanContext::Get().GetPhysicalDevice();
		SS_CORE_ASSERT(physDevice != VK_NULL_HANDLE, "VulkanRendererAPI: PhysicalDevice is null");
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physDevice, &props);
		return props.deviceName;
	}

	bool VulkanRendererAPI::IsRayTracingSupported() const
	{
		return VulkanContext::Get().SupportsRayTracing();
	}

	bool VulkanRendererAPI::IsOpacityMicromapSupported() const
	{
		return VulkanContext::Get().SupportsOpacityMicromap();
	}

	const std::vector<std::string>& VulkanRendererAPI::GetGpuNames() const
	{
		return VulkanContext::Get().GetGpuNames();
	}

	int VulkanRendererAPI::GetSelectedGpuIndex() const
	{
		return VulkanContext::Get().GetSelectedGpuIndex();
	}

	bool VulkanRendererAPI::IsFloat16Supported() const
	{
		return VulkanContext::Get().SupportsFloat16();
	}

	uint32_t VulkanRendererAPI::GetMaxSampleCount() const
	{
		const VkPhysicalDevice physDevice = VulkanContext::Get().GetPhysicalDevice();
		SS_CORE_ASSERT(physDevice != VK_NULL_HANDLE, "VulkanRendererAPI: PhysicalDevice is null");
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(physDevice, &props);

		// Only counts usable for BOTH color and depth (a forward MSAA pass has both attachments).
		const VkSampleCountFlags counts =
		    props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
		if (counts & VK_SAMPLE_COUNT_8_BIT)
			return 8;
		if (counts & VK_SAMPLE_COUNT_4_BIT)
			return 4;
		if (counts & VK_SAMPLE_COUNT_2_BIT)
			return 2;
		return 1;
	}

	Ref<CommandContext> VulkanRendererAPI::GetGraphicsCommandContext()
	{
		return m_GraphicsContexts[m_CurrentFrameIndex][m_CurrentGraphicsSegment];
	}

	void VulkanRendererAPI::AppendCollectedScopes(VulkanCommandContext& ctx)
	{
		std::vector<GpuScope> scopes = ctx.CollectGpuScopes();
		m_FrameGpuScopes.insert(m_FrameGpuScopes.end(),
		                        std::make_move_iterator(scopes.begin()),
		                        std::make_move_iterator(scopes.end()));
	}

	bool VulkanRendererAPI::IsAsyncComputeAvailable() const
	{
		// The timeline only exists when the device exposed a dedicated compute family (see Init), so its
		// presence IS the device-capability check. The CVar is the runtime A/B switch on top of it.
		return m_Timeline != VK_NULL_HANDLE && CVars::AsyncCompute.Get();
	}

	Ref<CommandContext> VulkanRendererAPI::ForkAsyncCompute()
	{
		SS_CORE_ASSERT(IsAsyncComputeAvailable(), "ForkAsyncCompute without async compute available");
		SS_CORE_ASSERT(!m_AsyncBatchOpen, "ForkAsyncCompute while a batch is already open");

		const uint32_t frame = m_CurrentFrameIndex;

		// Close the graphics segment that was recording; it is already the back of the submit list (seeded in
		// BeginFrame or pushed by the previous Join), so record the fork value on it rather than pushing again.
		// The compute batch waits on that value, so everything recorded before this point is visible to it.
		const uint64_t forkValue = ++m_TimelineNext;
		const Ref<VulkanCommandContext> gfx = m_GraphicsContexts[frame][m_CurrentGraphicsSegment];
		gfx->End();
		m_FrameGraphicsSubmits.back().SignalTimeline = forkValue;

		// Open (allocating on first use) the async-compute buffer for this batch.
		if (m_CurrentComputeBatch >= m_ComputeContexts[frame].size())
		{
			m_ComputeContexts[frame].push_back(CreateRef<VulkanCommandContext>(GpuQueue::AsyncCompute));
		}
		const Ref<VulkanCommandContext> comp = m_ComputeContexts[frame][m_CurrentComputeBatch];
		comp->Begin();
		AppendCollectedScopes(*comp);

		// The join value is reserved now so the batch's submit record is complete; JoinAsyncCompute opens the
		// next graphics segment waiting on it.
		const uint64_t joinValue = ++m_TimelineNext;
		m_FrameComputeSubmits.push_back({.Ctx = comp, .WaitTimeline = forkValue, .SignalTimeline = joinValue});

		m_AsyncBatchOpen = true;
		return comp;
	}

	void VulkanRendererAPI::JoinAsyncCompute()
	{
		SS_CORE_ASSERT(m_AsyncBatchOpen, "JoinAsyncCompute without an open batch");

		const uint32_t frame = m_CurrentFrameIndex;

		m_ComputeContexts[frame][m_CurrentComputeBatch]->End();
		const uint64_t joinValue = m_FrameComputeSubmits.back().SignalTimeline;
		++m_CurrentComputeBatch;
		m_AsyncBatchOpen = false;

		// Open the next graphics segment; it waits on the compute batch, so passes recorded from here on may
		// read what the batch wrote.
		++m_CurrentGraphicsSegment;
		if (m_CurrentGraphicsSegment >= m_GraphicsContexts[frame].size())
		{
			m_GraphicsContexts[frame].push_back(CreateRef<VulkanCommandContext>(GpuQueue::Graphics));
		}
		const Ref<VulkanCommandContext> gfx = m_GraphicsContexts[frame][m_CurrentGraphicsSegment];
		gfx->Begin();
		AppendCollectedScopes(*gfx);
		m_FrameGraphicsSubmits.push_back({.Ctx = gfx, .WaitTimeline = joinValue});
	}

	void VulkanRendererAPI::InitImGuiBackend(void* windowHandle)
	{
		auto& context = VulkanContext::Get();

		// 1. Load Vulkan functions for ImGui using Volk
		ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* user_data)
		                               { return vkGetInstanceProcAddr(VulkanContext::Get().GetInstance(), function_name); }, nullptr);

		// 2. Create Descriptor Pool for ImGui
		VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
		pool_info.pPoolSizes = pool_sizes;
		vkCreateDescriptorPool(context.GetDevice(), &pool_info, nullptr, &s_ImGuiPool);

		// 3. Init GLFW Platform backend
		GLFWwindow* window = static_cast<GLFWwindow*>(windowHandle);
		ImGui_ImplGlfw_InitForVulkan(window, true);

		// 4. Init ImGui
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = context.GetInstance();
		init_info.PhysicalDevice = context.GetPhysicalDevice();
		init_info.Device = context.GetDevice();
		init_info.Queue = context.GetGraphicsQueue();
		init_info.DescriptorPool = s_ImGuiPool;
		init_info.MinImageCount = 2;
		init_info.ImageCount = 3;
		init_info.UseDynamicRendering = true;

		init_info.CheckVkResultFn = [](const VkResult result)
		{
			if (result == VK_SUCCESS)
				return;
			SS_CORE_ERROR("[ImGui][Vulkan] Error: {0}", static_cast<int>(result));
		};

		static VkFormat surfaceFormat;
		surfaceFormat = context.GetSwapchainFormat();

		init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
		init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &surfaceFormat;

		ImGui_ImplVulkan_Init(&init_info);
	}

	void VulkanRendererAPI::ShutdownImGuiBackend()
	{
		const VkDevice device = VulkanContext::Get().GetDevice();
		vkDeviceWaitIdle(device);

		// Shutdown backends in reverse order of initialization
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();

		if (s_ImGuiPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, s_ImGuiPool, nullptr);
			s_ImGuiPool = VK_NULL_HANDLE;
		}
	}

	void VulkanRendererAPI::ImGuiNewFrame()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
	}

	void VulkanRendererAPI::RenderImGuiDrawData(CommandContext& context)
	{
		auto& vkContext = dynamic_cast<VulkanCommandContext&>(context);
		VkCommandBuffer cmd = vkContext.GetVulkanCommandBuffer();

		// 1. Ensure the viewport and scissor cover the whole screen before ImGui starts
		// ImGui_ImplVulkan_RenderDrawData will set its own internal scissors,
		// but it needs a clean slate.
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(VulkanContext::Get().GetSwapchainExtent().width);
		viewport.height = static_cast<float>(VulkanContext::Get().GetSwapchainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		// Only call Render() if the frame hasn't been finalized yet.
		// If the user hasn't called ImGui::NewFrame() at all, this will still assert,
		// which is GOOD because it highlights the logic error.
		if (ImGui::GetDrawData() == nullptr)
		{
			ImGui::Render();
		}

		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData && drawData->CmdListsCount > 0)
		{
			ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
		}
	}
}
