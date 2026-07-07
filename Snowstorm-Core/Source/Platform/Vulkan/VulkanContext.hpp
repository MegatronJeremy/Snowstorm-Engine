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

		// Rebuild the swapchain + image views after the surface changes (window resize / out-of-date).
		// Blocks until the GPU is idle first. Returns false (and leaves the old swapchain torn down)
		// when the surface area is zero, e.g. a minimized window — the caller should skip the frame.
		bool RecreateSwapchain();

		// VSync: true → FIFO (locked to refresh, no tearing). false → MAILBOX if supported (uncapped,
		// no tearing), else IMMEDIATE (uncapped, may tear). Changing this recreates the swapchain.
		void SetVSync(bool enabled);
		[[nodiscard]] bool IsVSync() const { return m_VSync; }

		static VulkanContext& Get();

		VkInstance GetInstance() const { return m_Instance; }

		VkDevice GetDevice() const { return m_Device; }

		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }

		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }

		uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamily; }

		// Dedicated transfer queue for async asset uploads (staging->image/buffer DMA off the graphics
		// queue). Falls back to the graphics queue/family when the GPU exposes no separate transfer family
		// — in that case HasDedicatedTransferQueue() is false and callers skip the cross-queue ownership
		// transfer (it would be a same-queue no-op).
		VkQueue GetTransferQueue() const { return m_TransferQueue; }
		uint32_t GetTransferQueueFamilyIndex() const { return m_TransferQueueFamily; }
		VkCommandPool GetTransferCommandPool() const { return m_TransferCommandPool; }
		[[nodiscard]] bool HasDedicatedTransferQueue() const { return m_TransferQueueFamily != m_GraphicsQueueFamily; }

		VmaAllocator GetAllocator() const { return m_Allocator; }

		VkCommandPool GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }

		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }

		VkFormat GetSwapchainFormat() const { return m_SwapchainFormat; }

		// True when VK_EXT_debug_utils is enabled (RenderDoc labels + object names usable).
		[[nodiscard]] bool IsDebugUtilsAvailable() const { return m_DebugUtilsAvailable; }

		VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }

		std::vector<VkImage> GetSwapchainImages() const { return m_SwapchainImages; }

	private:
		// (Re)build m_Swapchain + image views from the current surface capabilities. Used by Init and
		// RecreateSwapchain. Returns false when the surface extent is zero (minimized window).
		bool CreateSwapchain();
		void DestroySwapchain() const;

		// Vulkan core
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;

		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_GraphicsQueueFamily = 0;

		// Transfer queue for async uploads. When the GPU has a dedicated transfer family these differ from
		// the graphics ones; otherwise they alias the graphics queue/family (same handle, same index).
		VkQueue m_TransferQueue = VK_NULL_HANDLE;
		uint32_t m_TransferQueueFamily = 0;
		VkCommandPool m_TransferCommandPool = VK_NULL_HANDLE;

		VmaAllocator m_Allocator = nullptr;

		VkCommandPool m_GraphicsCommandPool = VK_NULL_HANDLE;

		// Debug messenger
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

		// True when VK_EXT_debug_utils was enabled on the instance (object naming + command labels).
		// Independent of validation layers — set from instance-extension enumeration in Init.
		bool m_DebugUtilsAvailable = false;

		// Pick the present mode honoring m_VSync from the surface's supported modes (FIFO is the only
		// one guaranteed by spec, so it's the universal fallback).
		[[nodiscard]] VkPresentModeKHR ChoosePresentMode() const;

		// Surface and swapchain
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		bool m_VSync = true;
		VkFormat m_SwapchainFormat{};
		VkExtent2D m_SwapchainExtent{};
		std::vector<VkImage> m_SwapchainImages;
		std::vector<VkImageView> m_SwapchainImageViews;

		// Window handle (used in Init)
		void* m_WindowHandle = nullptr;
	};
}
