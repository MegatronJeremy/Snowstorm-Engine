#include "VulkanRendererAPI.hpp"

// #define IMGUI_IMPL_VULKAN_NO_PROTOTYPES
// #define IMGUI_IMPL_VULKAN_USE_VOLK
#include "imgui_impl_vulkan.h"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanCommon.hpp"
#include "Platform/Vulkan/VulkanCommandContext.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"

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
	}

	void VulkanRendererAPI::Shutdown()
	{
		const VkDevice device = VulkanContext::Get().GetDevice();

		// Wait for GPU to finish work before destroying core context
		vkDeviceWaitIdle(device);

		for (uint32_t i = 0; i < s_MaxFramesInFlight; ++i)
		{
			vkDestroySemaphore(device, m_ImageAvailableSemaphores[i], nullptr);
			vkDestroySemaphore(device, m_RenderFinishedSemaphores[i], nullptr);
			vkDestroyFence(device, m_InFlightFences[i], nullptr);
		}

		m_GraphicsContexts.clear();

		// 2. Shut down the low-level context (Allocator, Device, Instance)
		VulkanContext::Get().Shutdown();
	}

	void VulkanRendererAPI::BeginFrame()
	{
		auto& context = VulkanContext::Get();
		VkDevice device = context.GetDevice();

		// 1. Wait for the GPU to finish the frame we are about to reuse
		vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrameIndex], VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrameIndex]);

		// 2. Acquire an image from the swapchain
		// Note: m_ImageIndex might be different from m_CurrentFrameIndex
		const VkResult result = vkAcquireNextImageKHR(
			device,
			context.GetSwapchain(),
			UINT64_MAX,
			m_ImageAvailableSemaphores[m_CurrentFrameIndex],
			VK_NULL_HANDLE,
			&m_ImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// Handle resize logic here later
			return;
		}

		// 3. Reset and Begin the command context for this frame
		auto ctx = std::static_pointer_cast<VulkanCommandContext>(m_GraphicsContexts[m_CurrentFrameIndex]);
		ctx->Begin(); 
	}

	void VulkanRendererAPI::EndFrame()
	{
		auto& context = VulkanContext::Get();
		auto ctx = std::static_pointer_cast<VulkanCommandContext>(m_GraphicsContexts[m_CurrentFrameIndex]);
		
		// 1. End command recording
		ctx->End();

		// 2. Submit to Queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrameIndex] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		VkCommandBuffer cmd = ctx->GetVulkanCommandBuffer();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrameIndex] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		vkQueueSubmit(context.GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrameIndex]);

		// 3. Present
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapchains[] = { context.GetSwapchain() };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &m_ImageIndex;

		vkQueuePresentKHR(context.GetGraphicsQueue(), &presentInfo);

		// 4. Advance frame counter
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
		ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* function_name, void* user_data) {
			return vkGetInstanceProcAddr(VulkanContext::Get().GetInstance(), function_name);
		}, nullptr);

		// 2. Create Descriptor Pool for ImGui
		VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 } };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
		pool_info.pPoolSizes = pool_sizes;
		vkCreateDescriptorPool(context.GetDevice(), &pool_info, nullptr, &s_ImGuiPool);

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
			if (result == VK_SUCCESS) return;
			SS_CORE_ERROR("[ImGui][Vulkan] Error: {0}", static_cast<int>(result));
		};

		VkFormat format = VK_FORMAT_B8G8R8A8_UNORM; // TODO Should ideally match swapchain
		init_info.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;

		ImGui_ImplVulkan_Init(&init_info);
	}

	void VulkanRendererAPI::ShutdownImGuiBackend()
	{
		vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(VulkanContext::Get().GetDevice(), s_ImGuiPool, nullptr);
	}

	void VulkanRendererAPI::ImGuiNewFrame()
	{
		ImGui_ImplVulkan_NewFrame();
	}

	void VulkanRendererAPI::RenderImGuiDrawData(CommandContext& context)
	{
		auto& vkContext = dynamic_cast<VulkanCommandContext&>(context);
		VkCommandBuffer cmd = vkContext.GetVulkanCommandBuffer();

		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}
}
