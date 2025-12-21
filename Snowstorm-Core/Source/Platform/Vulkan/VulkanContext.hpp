#pragma once

// --- Volk: function pointer loading ---
#define VK_NO_PROTOTYPES // tell vulkan headers not to include function declarations
#include <volk.h>

// --- VMA: Vulkan Memory Allocator ---
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

// --- Stdlib ---
#include <cassert>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	class VulkanContext
	{
	public:
		void Init(void* windowHandle); // GLFWwindow*, HWND, etc.
		void Shutdown() const;

		static VulkanContext& Get();

		VkInstance GetInstance() const { return m_Instance; }

		VkDevice GetDevice() const { return m_Device; }

		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }

		uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamily; }

		VmaAllocator GetAllocator() const { return m_Allocator; }

		VkCommandPool GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }

		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }

	private:
		// Vulkan core
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;

		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_GraphicsQueueFamily = 0;

		VmaAllocator m_Allocator = nullptr;

		VkCommandPool m_GraphicsCommandPool = VK_NULL_HANDLE;

		// Surface and swapchain
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkFormat m_SwapchainFormat{};
		VkExtent2D m_SwapchainExtent{};
		std::vector<VkImage> m_SwapchainImages;
		std::vector<VkImageView> m_SwapchainImageViews;

		// Window handle (used in Init)
		void* m_WindowHandle = nullptr;
	};
}
