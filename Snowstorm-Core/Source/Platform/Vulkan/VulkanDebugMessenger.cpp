#include "pch.h"

#include "VulkanDebugMessenger.h"

#include <vulkan/vk_platform.h>

namespace Snowstorm
{
	namespace
	{
		// Helper functions
		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
			const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData)
		{
			const char* message = pCallbackData->pMessage;

			switch (messageSeverity)
			{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				SS_CORE_TRACE(message);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				SS_CORE_INFO(message);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				SS_CORE_WARN(message);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				SS_CORE_ERROR(message);
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
				SS_CORE_ASSERT(false, "Unknown severity level!")
				break;
			}
			// always return VK_FALSE,
			// VK_ERROR_VALIDATION_FAILED_EXT used only to test validation layers themselves
			return VK_FALSE;
		}

		// proxy functions - stub needs to be loaded in since it is an extension
		VkResult CreateDebugUtilsMessengerEXT(const VkInstance instance,
		                                      const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		                                      const VkAllocationCallbacks* pAllocator,
		                                      VkDebugUtilsMessengerEXT* pDebugMessenger)
		{
			if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
					vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
				func != nullptr)
			{
				return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
			}
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}

		void DestroyDebugUtilsMessengerEXT(const VkInstance instance,
		                                   const VkDebugUtilsMessengerEXT debugMessenger,
		                                   const VkAllocationCallbacks* pAllocator)
		{
			if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
					vkGetInstanceProcAddr(
						instance, "vkDestroyDebugUtilsMessengerEXT"));
				func != nullptr)
			{
				func(instance, debugMessenger, pAllocator);
			}
		}
	}


	VulkanDebugMessenger::VulkanDebugMessenger(const VkInstance instance)
	{
		this->m_Instance = instance;

		VkDebugUtilsMessengerCreateInfoEXT createInfo;
		PopulateDebugMessengerCreateInfo(createInfo);

		// this is only for our specific Vulkan m_Instance and its layers
		const VkResult result = CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &m_VkDebugMessenger);

		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to set up debug messenger!")
	}

	void VulkanDebugMessenger::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
	{
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

		// specify all the severities the callback should be called for
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

		// similarly - filter the types of messages your callback is notified about
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

		createInfo.pfnUserCallback = VulkanDebugCallback;

		// can pass a pointer to the Application, for example
		createInfo.pUserData = nullptr; // optional
	}

	VulkanDebugMessenger::~VulkanDebugMessenger()
	{
		DestroyDebugUtilsMessengerEXT(m_Instance, m_VkDebugMessenger, nullptr);
	}
}
