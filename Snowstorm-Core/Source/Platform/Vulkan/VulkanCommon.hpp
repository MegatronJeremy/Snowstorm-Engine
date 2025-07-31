#pragma once

#include "VulkanContext.hpp"

namespace Snowstorm::VulkanCommon
{
	// Access to Vulkan handles
	VulkanContext& GetVulkanContext();
	VkInstance GetVulkanInstance();
	VkDevice GetVulkanDevice();
	VkPhysicalDevice GetVulkanPhysicalDevice();
	VkQueue GetGraphicsQueue();
	uint32_t GetGraphicsQueueFamilyIndex();
	VmaAllocator GetAllocator();
}
