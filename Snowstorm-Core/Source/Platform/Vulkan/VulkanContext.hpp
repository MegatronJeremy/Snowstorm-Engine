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
#include <string>
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

		// Independent compute front-end (AMD ACE, NVIDIA compute-only family) whose work runs concurrently
		// with graphics rather than serializing behind it.
		//
		// Aliases the graphics queue when the GPU exposes no COMPUTE-without-GRAPHICS family. Callers must
		// then stay on the graphics path: submitting to the aliased queue serializes anyway while still
		// paying cross-queue sync and ownership transfer, which is strictly worse than not splitting.
		VkQueue GetComputeQueue() const { return m_ComputeQueue; }
		uint32_t GetComputeQueueFamilyIndex() const { return m_ComputeQueueFamily; }
		VkCommandPool GetComputeCommandPool() const { return m_ComputeCommandPool; }
		[[nodiscard]] bool HasDedicatedComputeQueue() const { return m_ComputeQueueFamily != m_GraphicsQueueFamily; }

		VmaAllocator GetAllocator() const { return m_Allocator; }

		VkCommandPool GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }

		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }

		VkFormat GetSwapchainFormat() const { return m_SwapchainFormat; }

		// True when VK_EXT_debug_utils is enabled (RenderDoc labels + object names usable).
		[[nodiscard]] bool IsDebugUtilsAvailable() const { return m_DebugUtilsAvailable; }

		// True when the picked device supports inline ray tracing (VK_KHR_ray_query +
		// VK_KHR_acceleration_structure, feature bits + extensions) and they were enabled at device creation.
		// Gates all RT features downstream (AS builds, RT shadow pass); false => the raster path runs. Set in
		// Init from a capability query on the chosen physical device (see QueryRayTracingSupport).
		[[nodiscard]] bool SupportsRayTracing() const { return m_RayTracingSupported; }
		// True when VK_EXT_opacity_micromap is enabled (extension + micromap feature bit, RT also supported).
		// Gates the OMM bake/attach path: with an OMM the hardware resolves cutout coverage during traversal
		// and invokes the any-hit alpha test only on UNKNOWN (edge) microtriangles. False (e.g. RDNA3) => masked
		// geometry falls back to the FORCE_NO_OPAQUE any-hit path alone. Set in Init from a capability query.
		[[nodiscard]] bool SupportsOpacityMicromap() const { return m_OpacityMicromapSupported; }

		// The graphics+present-capable physical devices enumerated at init (in candidate-index order, matching
		// the render.gpu CVar's index selection), and which one was chosen. For the editor's GPU picker.
		[[nodiscard]] const std::vector<std::string>& GetGpuNames() const { return m_GpuNames; }
		[[nodiscard]] int GetSelectedGpuIndex() const { return m_SelectedGpuIndex; }
		// fp16 shader math + 16-bit storage (shaderFloat16 + storageBuffer16BitAccess). Gates the neural conv's
		// fp16 permutation; false => the fp32 path runs. Set in Init from a capability query.
		[[nodiscard]] bool SupportsFloat16() const { return m_Float16Supported; }

		// True when VK_EXT_device_fault is enabled: on a VK_ERROR_DEVICE_LOST we can query vendor fault info
		// (faulting addresses + vendor description) via LogDeviceFaultInfo. Debug-only diagnostic.
		[[nodiscard]] bool SupportsDeviceFault() const { return m_DeviceFaultSupported; }

		// On a VK_ERROR_DEVICE_LOST, dump whatever VK_EXT_device_fault can tell us about the GPU fault (address
		// records + a vendor-specific binary/description) at [error] level. No-op if the extension isn't
		// enabled. Call this right where a submit/present returns DEVICE_LOST, before the process dies — it's
		// the only structured signal a headless run gets about *where* the GPU died (see VulkanCommon submit).
		void LogDeviceFaultInfo() const;

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

		// Async-compute queue. Aliases the graphics queue/family/pool when the GPU has no dedicated
		// COMPUTE-without-GRAPHICS family (see HasDedicatedComputeQueue).
		VkQueue m_ComputeQueue = VK_NULL_HANDLE;
		uint32_t m_ComputeQueueFamily = 0;
		VkCommandPool m_ComputeCommandPool = VK_NULL_HANDLE;

		VmaAllocator m_Allocator = nullptr;

		VkCommandPool m_GraphicsCommandPool = VK_NULL_HANDLE;

		// Debug messenger
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

		// True when VK_EXT_debug_utils was enabled on the instance (object naming + command labels).
		// Independent of validation layers — set from instance-extension enumeration in Init.
		bool m_DebugUtilsAvailable = false;

		// True when VK_KHR_ray_query + VK_KHR_acceleration_structure (features + extensions) are supported on
		// the picked device and were enabled at device creation. Gates the whole RT path (#118).
		bool m_RayTracingSupported = false;
		bool m_OpacityMicromapSupported = false;
		std::vector<std::string> m_GpuNames; // graphics+present candidates, candidate-index order
		int m_SelectedGpuIndex = 0;          // index into m_GpuNames of the chosen device

		// True when shaderFloat16 + 16-bit storage are supported and were enabled. Gates the neural conv's fp16
		// permutation (# fp16 inference); false => fp32 fallback.
		bool m_Float16Supported = false;

		// True when VK_EXT_device_fault is supported on the picked device and was enabled at device creation.
		// Debug-only (a diagnostic, not a runtime feature). Gates LogDeviceFaultInfo.
		bool m_DeviceFaultSupported = false;

		// Query the picked physical device for inline ray-tracing support: the rayQuery + accelerationStructure
		// feature bits AND the three device extensions (acceleration_structure, ray_query, deferred_host_operations).
		// Returns true only when all are present; called before device creation to decide whether to enable them.
		[[nodiscard]] bool QueryRayTracingSupport() const;

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
