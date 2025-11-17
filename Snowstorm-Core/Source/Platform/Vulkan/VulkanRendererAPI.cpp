#include "pch.h"
#include "Platform/Vulkan/VulkanRendererAPI.hpp"

#include "VulkanCommon.hpp"


#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanInstance.h"
#include "VulkanSwapChain.h"
#include "VulkanCommandPool.h"
#include "VulkanContext.hpp"

namespace Snowstorm
{
	void VulkanRendererAPI::Init()
	{
		SS_PROFILE_FUNCTION();

		VkDevice device = VulkanInstance::GetInstance()->GetVulkanDevice()->GetVkDevice();
		VkCommandPool commandPool = *VulkanInstance::GetInstance()->GetVulkanCommandPool();

		m_VulkanCommandBuffer = CreateScope<VulkanCommandBuffers>(device, commandPool, 1);
	}

	void VulkanRendererAPI::SetViewport(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height)
	{
		// this is a delayed submission of a render command
		// Set viewport
		VkViewport viewport{};
		viewport.x = static_cast<float>(x);
		viewport.y = static_cast<float>(y);
		viewport.width = static_cast<float>(width);
		viewport.height = static_cast<float>(height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VulkanSwapChain::SetViewport(viewport);
	}
}
