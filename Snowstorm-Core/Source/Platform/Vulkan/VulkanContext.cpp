#include "VulkanContext.hpp"
#include <GLFW/glfw3.h>
#include <iostream>

#define VK_CHECK(x) do { VkResult err = x; if (err) { std::cerr << "Vulkan error: " << err << std::endl; abort(); } } while (0)

namespace Snowstorm
{
	VulkanContext& VulkanContext::Get()
	{
		static VulkanContext instance;
		return instance;
	}

	void VulkanContext::Init(void* windowHandle)
	{
		m_WindowHandle = windowHandle;

		// 1. Instance
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Snowstorm Engine";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Snowstorm";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;

		uint32_t glfwExtCount = 0;
		const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = glfwExtCount;
		createInfo.ppEnabledExtensionNames = glfwExts;

		VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_Instance));
		volkLoadInstance(m_Instance);

		// 2. Surface (GLFW)
		GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_WindowHandle);
		VK_CHECK(glfwCreateWindowSurface(m_Instance, glfwWindow, nullptr, &m_Surface));

		// 3. Physical Device
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

		for (auto& device : devices)
		{
			// For simplicity, just pick the first that supports graphics + present
			uint32_t queueCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
			std::vector<VkQueueFamilyProperties> props(queueCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, props.data());

			for (uint32_t i = 0; i < queueCount; ++i)
			{
				VkBool32 presentSupport = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
				if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport)
				{
					m_PhysicalDevice = device;
					m_GraphicsQueueFamily = i;
					break;
				}
			}
			if (m_PhysicalDevice != VK_NULL_HANDLE)
				break;
		}

		// 4. Logical Device
		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreate{};
		queueCreate.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreate.queueFamilyIndex = m_GraphicsQueueFamily;
		queueCreate.queueCount = 1;
		queueCreate.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures features{};
		VkDeviceCreateInfo devInfo{};
		devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		devInfo.queueCreateInfoCount = 1;
		devInfo.pQueueCreateInfos = &queueCreate;
		devInfo.pEnabledFeatures = &features;

		VK_CHECK(vkCreateDevice(m_PhysicalDevice, &devInfo, nullptr, &m_Device));
		volkLoadDevice(m_Device);
		vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);

		// 5. VMA Allocator
		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.device = m_Device;
		allocatorInfo.physicalDevice = m_PhysicalDevice;
		allocatorInfo.instance = m_Instance;
		VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator));

		// 6. Swapchain (minimal version)
		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);
		m_SwapchainExtent = caps.currentExtent;

		VkSurfaceFormatKHR surfaceFormat;
		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());
		surfaceFormat = formats[0];
		m_SwapchainFormat = surfaceFormat.format;

		VkSwapchainCreateInfoKHR swapInfo{};
		swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapInfo.surface = m_Surface;
		swapInfo.minImageCount = caps.minImageCount;
		swapInfo.imageFormat = m_SwapchainFormat;
		swapInfo.imageColorSpace = surfaceFormat.colorSpace;
		swapInfo.imageExtent = m_SwapchainExtent;
		swapInfo.imageArrayLayers = 1;
		swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapInfo.preTransform = caps.currentTransform;
		swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		swapInfo.clipped = VK_TRUE;

		VK_CHECK(vkCreateSwapchainKHR(m_Device, &swapInfo, nullptr, &m_Swapchain));

		uint32_t imageCount = 0;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
		m_SwapchainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

		m_SwapchainImageViews.resize(imageCount);
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_SwapchainImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = m_SwapchainFormat;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainImageViews[i]));
		}
	}

	void VulkanContext::Shutdown() const
	{
		for (auto view : m_SwapchainImageViews)
			vkDestroyImageView(m_Device, view, nullptr);

		vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		vmaDestroyAllocator(m_Allocator);
		vkDestroyDevice(m_Device, nullptr);
		vkDestroyInstance(m_Instance, nullptr);
	}
}
