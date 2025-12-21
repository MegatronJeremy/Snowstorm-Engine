#include "VulkanContext.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>

//-- compile the actual implementation in this file
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define VK_CHECK(expr)                               \
{                                                    \
	VkResult _vk_result = (expr);                    \
	SS_CORE_ASSERT(_vk_result == VK_SUCCESS);        \
}

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

		// 0. Initialize Volk
		// We use a local check to ensure we don't re-init if already done
		if (volkGetLoadedInstance() == VK_NULL_HANDLE)
		{
			VkResult volkRes = volkInitialize();
			SS_CORE_ASSERT(volkRes == VK_SUCCESS, "Failed to initialize Volk loader");
		}

		// 1. Instance
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Snowstorm Engine";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Snowstorm";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;

		uint32_t glfwExtCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtCount);

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = glfwExtCount;
		createInfo.ppEnabledExtensionNames = glfwExtensions;

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

		// Core features (extend as needed)
		VkPhysicalDeviceFeatures features{};

		// Common device extensions
		const char* deviceExtensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
		};

		VkDeviceCreateInfo devInfo{};
		devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		devInfo.queueCreateInfoCount = 1;
		devInfo.pQueueCreateInfos = &queueCreate;
		devInfo.pEnabledFeatures = &features;
		devInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(deviceExtensions));
		devInfo.ppEnabledExtensionNames = deviceExtensions;

		// Enable Dynamic Rendering and Buffer Device Address features
		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;

		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.bufferDeviceAddress = VK_TRUE;
		features12.pNext = &features13;

		devInfo.pNext = &features12;

		VK_CHECK(vkCreateDevice(m_PhysicalDevice, &devInfo, nullptr, &m_Device));
		volkLoadDevice(m_Device);
		vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);

		// 5. Graphics command pool (for transient command buffers)
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_GraphicsCommandPool));

		// 6. VMA Allocator
		VmaVulkanFunctions vmaFunctions{};
		vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorInfo.physicalDevice = m_PhysicalDevice;
		allocatorInfo.device = m_Device;
		allocatorInfo.instance = m_Instance;
		allocatorInfo.pVulkanFunctions = &vmaFunctions;

		// Required to allow buffers with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

		VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator));

		// 7. Swapchain (minimal version)
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
		for (const auto view : m_SwapchainImageViews)
		{
			vkDestroyImageView(m_Device, view, nullptr);
		}

		vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		vmaDestroyAllocator(m_Allocator);

		if (m_GraphicsCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_Device, m_GraphicsCommandPool, nullptr);
		}

		vkDestroyDevice(m_Device, nullptr);
		vkDestroyInstance(m_Instance, nullptr);
	}
}
