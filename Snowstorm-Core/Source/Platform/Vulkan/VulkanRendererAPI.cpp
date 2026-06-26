#include "VulkanRendererAPI.hpp"

#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "VulkanBindlessManager.hpp"

#include "Snowstorm/Core/Base.hpp"
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
		m_ImageAvailableSemaphores.resize(s_MaxFramesInFlight);
		m_RenderFinishedSemaphores.resize(s_MaxFramesInFlight);
		m_InFlightFences.resize(s_MaxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled so we don't wait forever on frame 0

		for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
		{
			m_GraphicsContexts[i] = CreateRef<VulkanCommandContext>();

			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]);
			vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFences[i]);
		}

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

	bool VulkanRendererAPI::RecreateSwapchain()
	{
		// VulkanContext::RecreateSwapchain drains the GPU before tearing down the old images, so the
		// Ref<VulkanTexture> wrappers we still hold are safe to drop and rebuild here.
		if (!VulkanContext::Get().RecreateSwapchain())
		{
			return false; // zero-extent surface (minimized) — skip the frame
		}
		WrapSwapchainTextures();
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

		// You must clear all swapchain textures we wrapped
		// These hold Ref<VulkanTexture> which own VMA allocations
		m_SwapchainTextures.clear();

		for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
		{
			vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
			vkDestroySemaphore(device, m_RenderFinishedSemaphores[i], nullptr);
			vkDestroyFence(device, m_InFlightFences[i], nullptr);
			if (m_TimestampPools[i] != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(device, m_TimestampPools[i], nullptr);
			}
		}

		m_GraphicsContexts.clear();

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

		auto ctx = std::static_pointer_cast<VulkanCommandContext>(m_GraphicsContexts[m_CurrentFrameIndex]);
		ctx->Begin();

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
		auto ctx = std::static_pointer_cast<VulkanCommandContext>(m_GraphicsContexts[m_CurrentFrameIndex]);

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
		VkSemaphoreSubmitInfo waitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
		waitInfo.semaphore = m_ImageAvailableSemaphores[m_CurrentFrameIndex];
		waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		VkSemaphoreSubmitInfo signalInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
		signalInfo.semaphore = m_RenderFinishedSemaphores[m_CurrentFrameIndex];
		signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		VkCommandBufferSubmitInfo cmdInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
		cmdInfo.commandBuffer = ctx->GetVulkanCommandBuffer();

		VkSubmitInfo2 submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
		submitInfo.waitSemaphoreInfoCount = 1;
		submitInfo.pWaitSemaphoreInfos = &waitInfo;
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = &cmdInfo;
		submitInfo.signalSemaphoreInfoCount = 1;
		submitInfo.pSignalSemaphoreInfos = &signalInfo;

		vkQueueSubmit2(context.GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrameIndex]);

		VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentFrameIndex]};

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

	Ref<CommandContext> VulkanRendererAPI::GetGraphicsCommandContext()
	{
		return m_GraphicsContexts[m_CurrentFrameIndex];
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
