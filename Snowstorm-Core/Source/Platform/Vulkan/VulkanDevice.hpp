#pragma once
#include <vector>

#include "VulkanInstance.h"

namespace Snowstorm
{
	class VulkanDevice
	{
	public:
		explicit VulkanDevice(VkSurfaceKHR surface);

		VulkanDevice(const VulkanDevice& other) = delete;
		VulkanDevice(VulkanDevice&& other) = delete;
		VulkanDevice& operator=(const VulkanDevice& other) = delete;
		VulkanDevice& operator=(VulkanDevice&& other) = delete;

		~VulkanDevice();

		VkPhysicalDevice GetVkPhysicalDevice() const
		{
			return m_PhysicalDevice;
		}

		VkDevice GetVkDevice() const
		{
			return m_Device;
		}

		VkQueue GetVkGraphicsQueue() const
		{
			return m_GraphicsQueue;
		}

		VkQueue GetVkPresentQueue() const
		{
			return m_PresentQueue;
		}

		operator VkDevice() const
		{
			return m_Device;
		}
		
	private:
		void PickPhysicalDevice();

		void CreateLogicalDevice();

		bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;

		int RateDeviceSuitability(VkPhysicalDevice device) const;

	private:
		VkPhysicalDevice m_PhysicalDevice;
		VkDevice m_Device; // logical device - to interface with the physical device

		VkSurfaceKHR m_Surface;

		VkQueue m_GraphicsQueue; // implicitly destroyed when the device is destroyed
		VkQueue m_PresentQueue;

		std::vector<const char*> m_DeviceExtensions = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		};
	};
}
