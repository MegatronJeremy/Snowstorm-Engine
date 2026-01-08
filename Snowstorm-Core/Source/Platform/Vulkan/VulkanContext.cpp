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

#include "VulkanBindlessManager.hpp"

#define VK_CHECK(expr)                                       \
{                                                            \
VkResult _vk_result = (expr);                                \
	if (_vk_result != VK_SUCCESS)                            \
	{                                                        \
		SS_CORE_ERROR("Vulkan Error: {0}", (int)_vk_result); \
		SS_CORE_ASSERT(_vk_result == VK_SUCCESS);            \
	}                                                        \
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

		bool enableValidationLayers = true; // Usually wrapped in #ifndef NDEBUG
		const char* validationLayers[] = {
			"VK_LAYER_KHRONOS_validation"
		};

		//-- Check if layers are actually available
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		bool layersFound = true;
		for (const char* layerName : validationLayers)
		{
			bool layerFound = false;
			for (const auto& layerProperties : availableLayers)
			{
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					layerFound = true;
					break;
				}
			}
			if (!layerFound)
			{
				layersFound = false;
				break;
			}
		}

		if (enableValidationLayers && !layersFound)
		{
			SS_CORE_WARN("Validation layers requested, but not available! Disabling...");
			enableValidationLayers = false;
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

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtCount);
		if (enableValidationLayers) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		if (enableValidationLayers)
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(std::size(validationLayers));
			createInfo.ppEnabledLayerNames = validationLayers;
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_Instance));
		volkLoadInstance(m_Instance);

		if (enableValidationLayers)
		{
			VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
			debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			                               VkDebugUtilsMessageTypeFlagsEXT type,
			                               const VkDebugUtilsMessengerCallbackDataEXT* data,
			                               void* user) -> VkBool32
			{
				SS_CORE_ERROR("Vulkan Validation Layer: {0}", data->pMessage);
				SS_CORE_ASSERT(false, "Validation failed!");
				return VK_TRUE;
			};

			VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_Instance, &debugInfo, nullptr, &m_DebugMessenger));
		}

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
		VkPhysicalDeviceFeatures supportedFeatures{};
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);

		VkPhysicalDeviceFeatures enabledFeatures{};
		if (supportedFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		} else {
			SS_CORE_WARN("Anisotropy not supported by hardware!"); // virtually supported by every GPU since 2010
		}

		enabledFeatures.samplerAnisotropy = VK_TRUE; // enabled by default in most engines

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
		devInfo.pEnabledFeatures = &enabledFeatures;
		devInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(deviceExtensions));
		devInfo.ppEnabledExtensionNames = deviceExtensions;

		// Enable Dynamic Rendering and Buffer Device Address features
		VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		features12.bufferDeviceAddress = VK_TRUE;

		// Bindless texture features
		features12.descriptorBindingPartiallyBound = VK_TRUE;
		features12.runtimeDescriptorArray = VK_TRUE;
		features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		features12.descriptorIndexing = VK_TRUE;

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
		vmaFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
		vmaFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		vmaFunctions.vkAllocateMemory = vkAllocateMemory;
		vmaFunctions.vkFreeMemory = vkFreeMemory;
		vmaFunctions.vkMapMemory = vkMapMemory;
		vmaFunctions.vkUnmapMemory = vkUnmapMemory;
		vmaFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
		vmaFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
		vmaFunctions.vkBindBufferMemory = vkBindBufferMemory;
		vmaFunctions.vkBindImageMemory = vkBindImageMemory;
		vmaFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
		vmaFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
		vmaFunctions.vkCreateBuffer = vkCreateBuffer;
		vmaFunctions.vkDestroyBuffer = vkDestroyBuffer;
		vmaFunctions.vkCreateImage = vkCreateImage;
		vmaFunctions.vkDestroyImage = vkDestroyImage;
		vmaFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorInfo.physicalDevice = m_PhysicalDevice;
		allocatorInfo.device = m_Device;
		allocatorInfo.instance = m_Instance;
		allocatorInfo.pVulkanFunctions = &vmaFunctions;

		// Required to allow buffers with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

		VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator));

		VulkanBindlessManager::Get().Init();

		// 7. Swapchain (minimal version)
		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);
		m_SwapchainExtent = caps.currentExtent;

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

		// --- PROPER SELECTION LOGIC ---
		VkSurfaceFormatKHR surfaceFormat = formats[0]; // fallback
		for (const auto& availableFormat : formats)
		{
			// We prefer BGRA8_UNORM with SRGB_NONLINEAR for standard desktop compatibility
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
				availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				surfaceFormat = availableFormat;
				break;
			}
		}
		m_SwapchainFormat = surfaceFormat.format;

		VkSwapchainCreateInfoKHR swapInfo{};
		swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapInfo.surface = m_Surface;
		swapInfo.minImageCount = caps.minImageCount;
		swapInfo.imageFormat = m_SwapchainFormat;
		swapInfo.imageColorSpace = surfaceFormat.colorSpace;
		swapInfo.imageExtent = m_SwapchainExtent;
		swapInfo.imageArrayLayers = 1;
		swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapInfo.preTransform = caps.currentTransform;
		swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // TODO Vsync is on -> make this configurable
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

		if (m_GraphicsCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_Device, m_GraphicsCommandPool, nullptr);
		}

		if (m_DebugMessenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
		}

		// char* statsString;
		// vmaBuildStatsString(m_Allocator, &statsString, VK_TRUE);
		// SS_CORE_INFO("VMA Leak Report: {0}", statsString);
		// vmaFreeStatsString(m_Allocator, statsString);

		vmaDestroyAllocator(m_Allocator);

		vkDestroyDevice(m_Device, nullptr);

		vkDestroyInstance(m_Instance, nullptr);
	}
}
