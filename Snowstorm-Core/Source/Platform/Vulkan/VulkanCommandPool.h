#pragma once
#include "VulkanDevice.hpp"

namespace Snowstorm
{
	class VulkanCommandPool
	{
	public:
		explicit VulkanCommandPool(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

		VulkanCommandPool(const VulkanCommandPool& other) = delete;
		VulkanCommandPool(VulkanCommandPool&& other) = delete;
		VulkanCommandPool& operator=(const VulkanCommandPool& other) = delete;
		VulkanCommandPool& operator=(VulkanCommandPool&& other) = delete;

		~VulkanCommandPool();

		operator VkCommandPool() const
		{
			return m_CommandPool;
		}

	private:
		VkDevice m_Device;

		VkCommandPool m_CommandPool;
	};
}
