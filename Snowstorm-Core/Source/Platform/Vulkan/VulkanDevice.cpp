#include "pch.h"

#include "VulkanDevice.hpp"

#include <set>

#include "VulkanQueueFamilyIndices.h"
#include "VulkanSwapChain.h"

namespace Snowstorm
{
	VulkanDevice::VulkanDevice(const VkSurfaceKHR surface)
	{
		this->m_Surface = surface;
		PickPhysicalDevice();
		CreateLogicalDevice();
	}

	VulkanDevice::~VulkanDevice()
	{
		vkDestroyDevice(m_Device, nullptr);
	}


	void VulkanDevice::PickPhysicalDevice()
	{
		const VkInstance vkInstance = VulkanInstance::GetInstance()->GetVulkanInstance();
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);

		SS_CORE_ASSERT(deviceCount != 0, "Failed to find GPUs with Vulkan support!");

		std::multimap<int, VkPhysicalDevice> candidates;

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());

		for (const auto& device : devices)
		{
			int score = RateDeviceSuitability(device);
			candidates.insert(std::make_pair(score, device));
		}

		if (candidates.rbegin()->first > 0)
		{
			m_PhysicalDevice = candidates.rbegin()->second;
		}
		else
		{
			SS_CORE_ASSERT(false, "Failed to find a suitable GPU!");
		}

// TODO-VK: Add #if ENABLE_LOGGING or something like that
		VkehysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
		SS_CORE_INFO("Selected GPU: {}", props.deviceName);
	}

	void VulkanDevice::CreateLogicalDevice()
	{
		const VulkanQueueFamilyIndices indices = VulkanQueueFamilyIndices::FindQueueFamilies(
			m_PhysicalDevice, m_Surface);

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

		std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

		constexpr float queuePriority = 1.0f;
		for (uint32_t queueFamily : uniqueQueueFamilies)
		{
			// we only need one for now - queue with graphics capabilities
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;

			// you don't really need more than one, since you can create all the command buffers on multiple threads
			// and submit them all at once without much overhead
			queueCreateInfo.queueCount = 1;

			// required even with single queue - for command scheduler - between 0.0 and 1.0
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}


		// next specify the set of device features that you'll be using
		VkPhysicalDeviceFeatures deviceFeatures{};
		// query available features
		VkPhysicalDeviceFeatures supportedFeatures;
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);
		// enable physical device features (maybe in some sort of array)
		// TODO-VK: will be supported in the future (maybe through some array defined in devices)
		// if (supportedFeatures.samplerAnisotropy)
		// {
		// 	deviceFeatures.samplerAnisotropy = VK_TRUE;
		// }

		// add dynamic rendering feature
		VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature{};
		dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
		// query dynamic rendering support (must be supported)
		VkPhysicalDeviceFeatures2 features2{}; // Features2 for extension features (1 is for core features only)
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &dynamicRenderingFeature;
		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);
		SS_CORE_ASSERT(dynamicRenderingFeature.dynamicRendering, "Dynamic rendering not supported by GPU");
		dynamicRenderingFeature.dynamicRendering = VK_TRUE;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = &dynamicRenderingFeature;

		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();

		createInfo.pEnabledFeatures = &deviceFeatures;

		// Next specify extensions/validation layers, but device specific this time
		// We only need to do this for backwards compatibility, though

		// The swap-chain extension though, has to be explicitly enabled at this point
		createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
		createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

		if (VulkanInstance::GetInstance()->EnabledValidationLayers())
		{
			auto& validationLayers = VulkanInstance::GetInstance()->GetValidationLayers();

			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		// Finally, instantiate the logical device
		const VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create logical device!");

		// get the actual graphics queue
		// these two queue families most likely the same - the handles will have the same value
		vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
		vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue);
	}

	bool VulkanDevice::CheckDeviceExtensionSupport(const VkPhysicalDevice device) const
	{
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(m_DeviceExtensions.begin(), m_DeviceExtensions.end());

		for (const auto& extension : availableExtensions)
		{
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
	}


	int VulkanDevice::RateDeviceSuitability(VkPhysicalDevice device) const
	{
		VkPhysicalDeviceProperties physicalDeviceProperties;
		VkPhysicalDeviceFeatures physicalDeviceFeatures;
		vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);
		vkGetPhysicalDeviceFeatures(device, &physicalDeviceFeatures);

		int score = 0;

		// Discrete GPUs have a significant performance advantage
		if (physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			score += 1000;
		}

		// Maximum possible size of textures affects graphics quality
		score += static_cast<int>(physicalDeviceProperties.limits.maxImageDimension2D);

		const VulkanQueueFamilyIndices indices = VulkanQueueFamilyIndices::FindQueueFamilies(device, m_Surface);

		const bool extensionsSupported = CheckDeviceExtensionSupport(device);

		bool swapChainAdequate = false;
		if (extensionsSupported)
		{
			VulkanSwapChainSupportDetails swapChainSupport = VulkanSwapChain::QuerySwapChainSupport(device, m_Surface);
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		// application can't function without valid queue families
		// or if swapchain is not supported (required device extensions)
		if (!indices.IsComplete() || !extensionsSupported || !swapChainAdequate)
		{
			return 0;
		}

		return score;
	}
}
