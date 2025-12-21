#pragma once

#include <functional>

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

	// Engine-level helpers

	// Shared graphics command pool
	// Used for transient command buffers (uploads, short GPU jobs)
	VkCommandPool GetGraphicsCommandPool();

	// For setup / infrequent uploads
	// For per-frame streaming, prefer a frame-level upload context instead
	void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);
}
