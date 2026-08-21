#include "VulkanContext.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Snowstorm/Core/EngineCVars.hpp"

//-- compile the actual implementation in this file
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "VulkanBindlessManager.hpp"

#define VK_CHECK(expr)                                           \
	{                                                            \
		VkResult _vk_result = (expr);                            \
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
		    "VK_LAYER_KHRONOS_validation"};

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
			// In Debug the layer path is baked in (SS_VULKAN_LAYER_PATH) and set in-process by EntryPoint, so a
			// missing layer here is a real misconfig worth a warning. In Release validation is deliberately not
			// compiled in and the layers aren't shipped, so this is by-design, not a problem — log it at info.
#ifdef SS_DEBUG
			SS_CORE_WARN("Validation layers requested, but not available! Disabling...");
#else
			SS_CORE_INFO("Validation not compiled into this build (Release); skipping.");
#endif
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

		// Enable VK_EXT_debug_utils whenever the runtime offers it, NOT only under validation. It provides
		// both object naming (SetVulkanObjectName) and command-buffer labels (BeginGpuScope -> RenderDoc
		// event regions); gating it on validation hid pass names in plain Debug/RenderDoc captures. The
		// validation *messenger* below stays validation-gated — this only enables the extension itself.
		{
			uint32_t extCount = 0;
			vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> available(extCount);
			vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
			for (const auto& ext : available)
			{
				if (strcmp(ext.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
				{
					m_DebugUtilsAvailable = true;
					break;
				}
			}
		}
		if (m_DebugUtilsAvailable)
		{
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

		// Opt-in deeper validation, built up from two independent tiers (both off by default — they add
		// overhead and best-practices is noisy):
		//   validation.extra -> synchronization (barrier/semaphore/fence hazards) + best-practices (foot-guns).
		//   validation.gpu   -> GPU-assisted validation: instruments shaders/AS builds on the device to catch
		//                       OOB descriptor / buffer-device-address access (a stale geometry-table read, a bad
		//                       BLAS reference) that CPU-side validation can't see. Much heavier; separate tier.
		std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures;
		if (enableValidationLayers && CVars::ValidationExtra.Get())
		{
			enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
			enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
		}
		if (enableValidationLayers && CVars::ValidationGpu.Get())
		{
			enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
			enabledValidationFeatures.push_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
		}

		VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
		validationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(enabledValidationFeatures.size());
		validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();

		if (!enabledValidationFeatures.empty())
		{
			validationFeatures.pNext = createInfo.pNext;
			createInfo.pNext = &validationFeatures;
			SS_CORE_INFO("Vulkan: extra validation enabled (extra={}, gpu-assisted={}).",
			             CVars::ValidationExtra.Get(), CVars::ValidationGpu.Get());
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
			debugInfo.pfnUserCallback = [](const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			                               VkDebugUtilsMessageTypeFlagsEXT /*type*/,
			                               const VkDebugUtilsMessengerCallbackDataEXT* data,
			                               void* /*user*/) -> VkBool32
			{
				// Active command-buffer label stack when the message fired: turns a bare validation string
				// into "[Forward > Sky] <message>" so an error names the pass it happened in. Populated by
				// our BeginDebugLabel scopes (RenderGraph per-pass) — empty outside any labeled region.
				std::string scope;
				for (uint32_t i = 0; i < data->cmdBufLabelCount; ++i)
				{
					scope += (i == 0) ? "[" : " > ";
					scope += data->pCmdBufLabels[i].pLabelName;
				}
				if (!scope.empty())
				{
					scope += "] ";
				}

				// Only ERROR severity should halt; warnings/info/verbose just log.
				if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				{
					SS_CORE_ERROR("Vulkan Validation: {0}{1}", scope, data->pMessage);

					// When validation.nonfatal is set, log every error and keep running instead of
					// asserting on the first one. Lets a single run (e.g. the smoke test) surface
					// ALL validation errors at once rather than one-per-run. Cached once; the lambda
					// is captureless (required for the C function-pointer conversion), and CVars are
					// resolved at startup before this callback can fire.
					static const bool s_NonFatal = CVars::ValidationNonFatal.Get();
					if (!s_NonFatal)
					{
						SS_CORE_ASSERT(false, "Vulkan validation error");
					}
				}
				else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
				{
					SS_CORE_WARN("Vulkan Validation: {0}{1}", scope, data->pMessage);
				}
				else
				{
					SS_CORE_TRACE("Vulkan Validation: {0}{1}", scope, data->pMessage);
				}
				// Per spec, apps must return VK_FALSE (VK_TRUE aborts the triggering call).
				return VK_FALSE;
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

		// Gather candidate devices (each with a graphics+present queue family) and log them so a multi-GPU box's
		// options + indices are visible. The old code just took the FIRST graphics+present device, which is
		// enumeration-order-dependent and on a 2-GPU box can land on the wrong card.
		struct GpuCandidate
		{
			VkPhysicalDevice Device;
			uint32_t GraphicsFamily;
			VkPhysicalDeviceProperties Props;
		};
		std::vector<GpuCandidate> candidates;
		for (auto& device : devices)
		{
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
					VkPhysicalDeviceProperties dp{};
					vkGetPhysicalDeviceProperties(device, &dp);
					candidates.push_back({device, i, dp});
					break;
				}
			}
		}
		SS_CORE_ASSERT(!candidates.empty(), "No Vulkan device with a graphics+present queue family");
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			SS_CORE_INFO("GPU candidate [{}]: {} (type {}).", i, candidates[i].Props.deviceName,
			             static_cast<int>(candidates[i].Props.deviceType));
		}

		// Pick per render.gpu: an all-digits value selects by candidate index; otherwise a case-insensitive
		// name substring. Empty (default) auto-selects the first DISCRETE GPU, else the first candidate.
		auto toLowerAscii = [](std::string s)
		{
			for (char& c : s)
			{
				if (c >= 'A' && c <= 'Z')
				{
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			return s;
		};
		size_t chosen = candidates.size(); // sentinel: unresolved
		if (const std::string& sel = CVars::GpuSelect.Get(); !sel.empty())
		{
			if (std::all_of(sel.begin(), sel.end(), [](unsigned char c)
			                { return c >= '0' && c <= '9'; }))
			{
				if (const size_t idx = static_cast<size_t>(std::stoul(sel)); idx < candidates.size())
				{
					chosen = idx;
				}
			}
			else
			{
				const std::string needle = toLowerAscii(sel);
				for (size_t i = 0; i < candidates.size(); ++i)
				{
					if (toLowerAscii(candidates[i].Props.deviceName).find(needle) != std::string::npos)
					{
						chosen = i;
						break;
					}
				}
			}
			if (chosen == candidates.size())
			{
				SS_CORE_WARN("render.gpu='{}' matched no candidate; using auto-select.", sel);
			}
		}
		if (chosen == candidates.size())
		{
			chosen = 0;
			for (size_t i = 0; i < candidates.size(); ++i)
			{
				if (candidates[i].Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					chosen = i;
					break;
				}
			}
		}

		m_PhysicalDevice = candidates[chosen].Device;
		m_GraphicsQueueFamily = candidates[chosen].GraphicsFamily;

		// Transfer family for the chosen device: prefer a DEDICATED transfer family (TRANSFER set, GRAPHICS/
		// COMPUTE clear) — the async-DMA path that uploads without contending the graphics queue. Fall back to
		// the graphics family when none exists (then m_TransferQueueFamily == m_GraphicsQueueFamily and
		// HasDedicatedTransferQueue() is false).
		{
			uint32_t queueCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueCount, nullptr);
			std::vector<VkQueueFamilyProperties> props(queueCount);
			vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueCount, props.data());
			m_TransferQueueFamily = m_GraphicsQueueFamily;
			for (uint32_t i = 0; i < queueCount; ++i)
			{
				const bool hasTransfer = (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
				const bool hasGraphics = (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
				const bool hasCompute = (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
				if (hasTransfer && !hasGraphics && !hasCompute)
				{
					m_TransferQueueFamily = i;
					break;
				}
			}
		}

		SS_CORE_INFO("Selected GPU [{}]: {} (of {} candidate(s)).", chosen, candidates[chosen].Props.deviceName,
		             candidates.size());

		// Record the candidate names + selection for the editor's GPU picker (index order matches render.gpu).
		m_GpuNames.reserve(candidates.size());
		for (const GpuCandidate& c : candidates)
		{
			m_GpuNames.emplace_back(c.Props.deviceName);
		}
		m_SelectedGpuIndex = static_cast<int>(chosen);

		// 4. Logical Device
		float queuePriority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueCreates;
		{
			VkDeviceQueueCreateInfo gfx{};
			gfx.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			gfx.queueFamilyIndex = m_GraphicsQueueFamily;
			gfx.queueCount = 1;
			gfx.pQueuePriorities = &queuePriority;
			queueCreates.push_back(gfx);

			// Request the transfer family too, but only when it's a distinct family (a second
			// VkDeviceQueueCreateInfo for the SAME family index is invalid).
			if (m_TransferQueueFamily != m_GraphicsQueueFamily)
			{
				VkDeviceQueueCreateInfo xfer{};
				xfer.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				xfer.queueFamilyIndex = m_TransferQueueFamily;
				xfer.queueCount = 1;
				xfer.pQueuePriorities = &queuePriority;
				queueCreates.push_back(xfer);
			}
		}

		// Core features (extend as needed)
		VkPhysicalDeviceFeatures supportedFeatures{};
		vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supportedFeatures);

		VkPhysicalDeviceFeatures enabledFeatures{};
		if (supportedFeatures.samplerAnisotropy)
		{
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
		else
		{
			// Don't force-enable an unsupported feature (device creation would fail/validate).
			SS_CORE_WARN("samplerAnisotropy not supported by hardware; disabling it.");
		}

		// 64-bit ints in shaders: needed by the RT reflection trace's device-address arithmetic
		// (vk::RawBufferLoad<uint64_t> over the geometry table, #118). Universally supported on RT-class
		// GPUs; enabled only when present so a device lacking it still creates (the RT permutation just won't
		// run there — same graceful-fallback contract as the RT extensions).
		if (supportedFeatures.shaderInt64)
		{
			enabledFeatures.shaderInt64 = VK_TRUE;
		}

		// Pipeline-statistics queries: per-pass fragment-shader-invocation counts for the headless overdraw
		// metric (VulkanCommandContext's second query pool, surfaced via perf-bench). Enabled only when
		// supported; the pool creation fail-soft skips if this is off, so a device without it just reports 0.
		if (supportedFeatures.pipelineStatisticsQuery)
		{
			enabledFeatures.pipelineStatisticsQuery = VK_TRUE;
		}

		// Common device extensions
		std::vector<const char*> deviceExtensions = {
		    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};

		// Inline ray tracing (#118). Only enabled when the device supports the feature bits AND the three
		// extensions — QueryRayTracingSupport checks all of them. On a non-RT GPU we skip both the extensions
		// and the feature structs, so vkCreateDevice still succeeds and the raster path runs unchanged.
		m_RayTracingSupported = QueryRayTracingSupport();
		if (m_RayTracingSupported)
		{
			deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
			deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
			// acceleration_structure requires deferred_host_operations to be enabled alongside it.
			deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

			// VK_EXT_opacity_micromap (OMM): lets the BLAS carry per-microtriangle opaque/transparent/unknown
			// state so the hardware resolves cutout coverage during traversal and calls the any-hit alpha test
			// only on UNKNOWN (edge) microtriangles. Requires acceleration_structure (above) + the micromap
			// feature bit. Availability-gated so a non-OMM GPU (RDNA3) still creates the device and masked
			// geometry falls back to the FORCE_NO_OPAQUE any-hit path.
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> avail(extCount);
			vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, avail.data());
			for (const auto& e : avail)
			{
				if (std::strcmp(e.extensionName, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME) == 0)
				{
					VkPhysicalDeviceOpacityMicromapFeaturesEXT ommQuery{
					    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};
					VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
					f2.pNext = &ommQuery;
					vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &f2);
					m_OpacityMicromapSupported = (ommQuery.micromap == VK_TRUE);
					break;
				}
			}
			if (m_OpacityMicromapSupported)
			{
				deviceExtensions.push_back(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
			}
		}

#ifdef SS_DEBUG
		// VK_EXT_device_fault (Debug only): lets us query WHERE the GPU died on a VK_ERROR_DEVICE_LOST — vendor
		// fault addresses + a description — instead of a bare "-4". A pure diagnostic, so it's Debug-gated and
		// only enabled when the device advertises it (many drivers don't). Checked against the device's
		// extension list so vkCreateDevice can't fail on an unsupported extension.
		{
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> avail(extCount);
			vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, avail.data());
			for (const auto& e : avail)
			{
				if (std::strcmp(e.extensionName, VK_EXT_DEVICE_FAULT_EXTENSION_NAME) == 0)
				{
					m_DeviceFaultSupported = true;
					deviceExtensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
					break;
				}
			}
		}
#endif

		VkDeviceCreateInfo devInfo{};
		devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		devInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreates.size());
		devInfo.pQueueCreateInfos = queueCreates.data();
		devInfo.pEnabledFeatures = &enabledFeatures;
		devInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
		devInfo.ppEnabledExtensionNames = deviceExtensions.data();

		// Enable Dynamic Rendering and Buffer Device Address features
		VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
		features12.bufferDeviceAddress = VK_TRUE;

		// Bindless texture features
		features12.descriptorBindingPartiallyBound = VK_TRUE;
		features12.runtimeDescriptorArray = VK_TRUE;
		features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
		features12.descriptorIndexing = VK_TRUE;
		// Required for NonUniformResourceIndex() in shaders: with instancing, one draw samples the
		// bindless array with an index that varies per instance (not dynamically uniform). Without this
		// feature + the NonUniformResourceIndex wrap, such reads are undefined (garbage/flicker).
		features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

		// fp16 shader math + 16-bit storage for the neural inference conv (#): halves the weight traffic and
		// doubles ALU on GPUs that support it. The bits live in TWO promoted structs: shaderFloat16 is in
		// VkPhysicalDeviceVulkan12Features (set on features12 directly — a separate ShaderFloat16Int8Features
		// alongside it is a validation error), while the 16-bit STORAGE bits are in VkPhysicalDeviceVulkan11Features
		// (spliced onto the chain). Query both the same way. Gated: only enable + let the shader take the fp16
		// permutation when all three are supported, else the conv stays fp32. Mirrors the RT-support gate.
		{
			VkPhysicalDeviceVulkan11Features q11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
			VkPhysicalDeviceVulkan12Features q12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
			q11.pNext = &q12;
			VkPhysicalDeviceFeatures2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
			q.pNext = &q11;
			vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &q);
			m_Float16Supported = q12.shaderFloat16 == VK_TRUE && q11.storageBuffer16BitAccess == VK_TRUE &&
			                     q11.uniformAndStorageBuffer16BitAccess == VK_TRUE;
		}
		if (m_Float16Supported)
		{
			features12.shaderFloat16 = VK_TRUE; // shaderFloat16 lives in the 1.2 struct
		}

		features12.pNext = &features13;

		// 16-bit STORAGE access lives in the 1.1 features struct (not 1.2). Splice it on when supported; storage
		// outside the if so it outlives vkCreateDevice, tail preserves the existing chain (features13 + RT below).
		VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
		if (m_Float16Supported)
		{
			features11.storageBuffer16BitAccess = VK_TRUE;
			features11.uniformAndStorageBuffer16BitAccess = VK_TRUE;
			features11.pNext = features12.pNext; // = features13
			features12.pNext = &features11;
		}

		devInfo.pNext = &features12;

		// Splice the RT feature structs onto the chain when supported (#118). accelerationStructure enables
		// building/binding AS; rayQuery enables the inline trace intrinsics in compute. Declared here (outside
		// the if) so their storage outlives the vkCreateDevice call below.
		VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
		VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		if (m_RayTracingSupported)
		{
			asFeatures.accelerationStructure = VK_TRUE;
			rayQueryFeatures.rayQuery = VK_TRUE;
			asFeatures.pNext = &rayQueryFeatures;
			rayQueryFeatures.pNext = features13.pNext; // preserve any existing tail (currently none)
			features13.pNext = &asFeatures;
		}

		// VK_EXT_device_fault feature (Debug diagnostic). Its deviceFault bit must be enabled for the fault-info
		// query to work; splice it onto the chain when the extension was added above. Storage outside the if so
		// it outlives vkCreateDevice; tail preserves whatever the chain already had.
		VkPhysicalDeviceFaultFeaturesEXT faultFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
		if (m_DeviceFaultSupported)
		{
			faultFeatures.deviceFault = VK_TRUE;
			faultFeatures.pNext = features13.pNext;
			features13.pNext = &faultFeatures;
		}

		// Opacity-micromap feature (OMM). Splice on when the extension was added above; storage outside the if so
		// it outlives vkCreateDevice, tail preserves whatever the chain already had.
		VkPhysicalDeviceOpacityMicromapFeaturesEXT ommFeatures{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT};
		if (m_OpacityMicromapSupported)
		{
			ommFeatures.micromap = VK_TRUE;
			ommFeatures.pNext = features13.pNext;
			features13.pNext = &ommFeatures;
		}

		VK_CHECK(vkCreateDevice(m_PhysicalDevice, &devInfo, nullptr, &m_Device));
		volkLoadDevice(m_Device);
		vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
		// Transfer queue: the dedicated family's queue when distinct, else the graphics queue handle (aliased).
		vkGetDeviceQueue(m_Device, m_TransferQueueFamily, 0, &m_TransferQueue);
		SS_CORE_INFO("Vulkan queues: graphics family {}, transfer family {}{}.",
		             m_GraphicsQueueFamily, m_TransferQueueFamily,
		             HasDedicatedTransferQueue() ? " (dedicated)" : " (shared with graphics)");
		SS_CORE_INFO("Ray tracing (VK_KHR_ray_query): {}.",
		             m_RayTracingSupported ? "supported (enabled)" : "not supported (raster fallback)");
		SS_CORE_INFO("Opacity micromaps (VK_EXT_opacity_micromap): {}.",
		             m_OpacityMicromapSupported ? "supported (enabled)" : "not supported (any-hit fallback)");
		SS_CORE_INFO("fp16 shader math (shaderFloat16 + 16-bit storage): {}.",
		             m_Float16Supported ? "supported (neural fp16 path enabled)" : "not supported (fp32 fallback)");
#ifdef SS_DEBUG
		SS_CORE_INFO("Device fault diagnostics (VK_EXT_device_fault): {}.",
		             m_DeviceFaultSupported ? "supported (enabled)" : "not supported");
#endif

		// 5. Graphics command pool (for transient command buffers)
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_GraphicsCommandPool));

		// Transfer command pool. A separate pool on the transfer family when dedicated; otherwise reuse the
		// graphics pool (same family — a distinct pool would be redundant).
		if (HasDedicatedTransferQueue())
		{
			VkCommandPoolCreateInfo xferPool{};
			xferPool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			xferPool.queueFamilyIndex = m_TransferQueueFamily;
			xferPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK(vkCreateCommandPool(m_Device, &xferPool, nullptr, &m_TransferCommandPool));
		}
		else
		{
			m_TransferCommandPool = m_GraphicsCommandPool;
		}

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

		// 7. Swapchain
		CreateSwapchain();
	}

	bool VulkanContext::QueryRayTracingSupport() const
	{
		// 1) The three device extensions must all be advertised. accelerationStructure pulls in
		//    deferred_host_operations as a hard dependency; ray_query is the inline-trace path we use.
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> available(extCount);
		vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, available.data());

		const char* required[] = {
		    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		    VK_KHR_RAY_QUERY_EXTENSION_NAME,
		    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
		for (const char* need : required)
		{
			const bool found = std::any_of(available.begin(), available.end(),
			                               [need](const VkExtensionProperties& e)
			                               { return std::strcmp(e.extensionName, need) == 0; });
			if (!found)
			{
				return false;
			}
		}

		// 2) The feature bits must be set. Extension advertised but feature off => can't enable it.
		VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
		VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
		as.pNext = &rq;
		VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		features2.pNext = &as;
		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &features2);

		return as.accelerationStructure == VK_TRUE && rq.rayQuery == VK_TRUE;
	}

	bool VulkanContext::CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &caps);

		// currentExtent is 0x0 when the window is minimized (or the surface is otherwise zero-area).
		// A zero-extent swapchain is invalid; bail and let the caller skip rendering until restore.
		if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
		{
			return false;
		}
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

		// Request one more than the surface minimum (min is usually 2 = double buffering, which locks the
		// GPU to an integer fraction of the refresh rate when it can't keep up). +1 gives triple buffering
		// so present has a spare image to work with. Clamp to maxImageCount (0 = no upper bound). This is
		// the driver-recommended default (Vulkan best-practices #31).
		uint32_t desiredImageCount = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && desiredImageCount > caps.maxImageCount)
		{
			desiredImageCount = caps.maxImageCount;
		}

		VkSwapchainCreateInfoKHR swapInfo{};
		swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapInfo.surface = m_Surface;
		swapInfo.minImageCount = desiredImageCount;
		swapInfo.imageFormat = m_SwapchainFormat;
		swapInfo.imageColorSpace = surfaceFormat.colorSpace;
		swapInfo.imageExtent = m_SwapchainExtent;
		swapInfo.imageArrayLayers = 1;
		swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapInfo.preTransform = caps.currentTransform;
		swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapInfo.presentMode = ChoosePresentMode();
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

		return true;
	}

	void VulkanContext::DestroySwapchain() const
	{
		for (const auto view : m_SwapchainImageViews)
		{
			vkDestroyImageView(m_Device, view, nullptr);
		}

		if (m_Swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
		}
	}

	VkPresentModeKHR VulkanContext::ChoosePresentMode() const
	{
		uint32_t count = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, nullptr);
		std::vector<VkPresentModeKHR> modes(count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, modes.data());

		const auto supports = [&](const VkPresentModeKHR m)
		{
			return std::ranges::find(modes, m) != modes.end();
		};

		// VSync on → FIFO (always supported, locked to refresh). VSync off → prefer MAILBOX (uncapped,
		// no tearing); fall back to IMMEDIATE (uncapped, may tear); FIFO if neither is available.
		if (m_VSync)
		{
			return VK_PRESENT_MODE_FIFO_KHR;
		}
		if (supports(VK_PRESENT_MODE_MAILBOX_KHR))
		{
			return VK_PRESENT_MODE_MAILBOX_KHR;
		}
		if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR))
		{
			return VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	void VulkanContext::SetVSync(const bool enabled)
	{
		// Only stores the desired mode; the caller (VulkanRendererAPI::SetVSync) drives the swapchain
		// recreate so the RHI's wrapped swapchain textures get rebuilt too. Recreating here would skip
		// that re-wrap and leave the RHI pointing at destroyed images.
		m_VSync = enabled;
	}

	bool VulkanContext::RecreateSwapchain()
	{
		// All in-flight work may still reference the old swapchain images; drain the GPU first.
		vkDeviceWaitIdle(m_Device);

		DestroySwapchain();

		// Reset handles so a failed (zero-extent) recreate leaves us in a clean, non-dangling state.
		m_Swapchain = VK_NULL_HANDLE;
		m_SwapchainImages.clear();
		m_SwapchainImageViews.clear();

		return CreateSwapchain();
	}

	void VulkanContext::Shutdown() const
	{
		DestroySwapchain();
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

		// Destroy the transfer pool first, but only if it's a distinct object (when shared with graphics it
		// aliases m_GraphicsCommandPool — destroying it here then again below would be a double-free).
		if (m_TransferCommandPool != VK_NULL_HANDLE && m_TransferCommandPool != m_GraphicsCommandPool)
		{
			vkDestroyCommandPool(m_Device, m_TransferCommandPool, nullptr);
		}

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

	void VulkanContext::LogDeviceFaultInfo() const
	{
		if (!m_DeviceFaultSupported || vkGetDeviceFaultInfoEXT == nullptr)
		{
			SS_CORE_ERROR("Device lost, but VK_EXT_device_fault is unavailable — no GPU fault detail. "
			              "(Run a Debug build on a device that supports it to get faulting addresses.)");
			return;
		}

		// Two-call idiom: first get the counts, then allocate + fetch. The address-info records name each
		// faulting GPU virtual address + its type (read/write/instruction-pointer/etc.), which for an OOB
		// buffer-device-address read (the geometry-table-index theory) points straight at the bad access.
		VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
		if (vkGetDeviceFaultInfoEXT(m_Device, &counts, nullptr) != VK_SUCCESS)
		{
			SS_CORE_ERROR("vkGetDeviceFaultInfoEXT (counts) failed after device loss.");
			return;
		}

		std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
		std::vector<VkDeviceFaultVendorInfoEXT> vendorInfos(counts.vendorInfoCount);
		VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
		info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
		info.pVendorInfos = vendorInfos.empty() ? nullptr : vendorInfos.data();
		if (vkGetDeviceFaultInfoEXT(m_Device, &counts, &info) != VK_SUCCESS)
		{
			SS_CORE_ERROR("vkGetDeviceFaultInfoEXT (fetch) failed after device loss.");
			return;
		}

		SS_CORE_ERROR("=== GPU DEVICE FAULT: {} === ({} address record(s), {} vendor record(s))",
		              info.description, counts.addressInfoCount, counts.vendorInfoCount);
		for (uint32_t i = 0; i < counts.addressInfoCount; ++i)
		{
			const VkDeviceFaultAddressInfoEXT& a = addresses[i];
			SS_CORE_ERROR("  fault addr[{}]: type={} reportedAddress=0x{:x} precision(addressMask)=0x{:x}",
			              i, static_cast<int>(a.addressType), a.reportedAddress, a.addressPrecision);
		}
		for (uint32_t i = 0; i < counts.vendorInfoCount; ++i)
		{
			const VkDeviceFaultVendorInfoEXT& v = vendorInfos[i];
			SS_CORE_ERROR("  vendor[{}]: '{}' code=0x{:x} data=0x{:x}", i, v.description, v.vendorFaultCode, v.vendorFaultData);
		}
	}
}
