#pragma once

#include "VulkanDebugMessenger.h"

namespace Snowstorm
{
	namespace Vulkan
	{
#define MAX_FRAMES_IN_FLIGHT 2
	}

	class VulkanDevice;
	class VulkanCommandPool;

	class VulkanInstance
	{
		// Singleton class that holds the vulkan instance
		// This m_Instance has all the app-related information stored in it
		// If multiple aps are required this singleton can be expanded to hold more instances
	public:
		static VulkanInstance* GetInstance()
		{
			if (m_Instance == nullptr)
			{
				m_Instance = new VulkanInstance;
			}
			return m_Instance;
		}

		VkInstance GetVulkanInstance() const
		{
			return m_VkInstance;
		}

		bool EnabledValidationLayers() const
		{
			return m_EnabledValidationLayers;
		}

		const std::vector<const char*>& GetValidationLayers() const
		{
			return m_ValidationLayers;
		}

		Ref<VulkanDevice> GetVulkanDevice() const;

		void SetVulkanDevice(const Ref<VulkanDevice>& vulkanDevice);

		Ref<VulkanCommandPool> GetVulkanCommandPool() const;

		void SetVulkanCommandPool(const Ref<VulkanCommandPool>& vulkanCommandPool);

	private:
		VulkanInstance();

		bool CheckValidationLayerSupport() const;
		std::vector<const char*> GetRequiredExtensions() const;

		void SetupDebugMessenger();

		static inline VulkanInstance* m_Instance = nullptr;

		VkInstance m_VkInstance = nullptr;

		std::vector<const char*> m_ValidationLayers = {
			"VK_LAYER_KHRONOS_validation"
		};

#ifdef SS_DEBUG
        bool m_EnabledValidationLayers = true;
#else
		bool m_EnabledValidationLayers = false;
#endif


		Scope<VulkanDebugMessenger> m_VulkanDebugMessenger = nullptr;

		Ref<VulkanDevice> m_VulkanDevice = nullptr;

		Ref<VulkanCommandPool> m_VulkanCommandPool = nullptr;
	};
}
