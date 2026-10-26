#include "pch.h"

#include "VulkanDeviceCaps.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Utility/EnginePaths.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Snowstorm::VulkanDeviceCaps
{
	namespace
	{
		bool HasExtension(const std::vector<VkExtensionProperties>& exts, const char* name)
		{
			for (const auto& e : exts)
			{
				if (std::strcmp(e.extensionName, name) == 0)
				{
					return true;
				}
			}
			return false;
		}

		std::string ComponentName(VkComponentTypeKHR t)
		{
			switch (t)
			{
			case VK_COMPONENT_TYPE_FLOAT16_KHR:
				return "f16";
			case VK_COMPONENT_TYPE_FLOAT32_KHR:
				return "f32";
			case VK_COMPONENT_TYPE_FLOAT64_KHR:
				return "f64";
			case VK_COMPONENT_TYPE_SINT8_KHR:
				return "s8";
			case VK_COMPONENT_TYPE_SINT16_KHR:
				return "s16";
			case VK_COMPONENT_TYPE_SINT32_KHR:
				return "s32";
			case VK_COMPONENT_TYPE_SINT64_KHR:
				return "s64";
			case VK_COMPONENT_TYPE_UINT8_KHR:
				return "u8";
			case VK_COMPONENT_TYPE_UINT16_KHR:
				return "u16";
			case VK_COMPONENT_TYPE_UINT32_KHR:
				return "u32";
			case VK_COMPONENT_TYPE_UINT64_KHR:
				return "u64";
			default:
				return "enum" + std::to_string(static_cast<int>(t));
			}
		}

		std::string ScopeName(VkScopeKHR s)
		{
			switch (s)
			{
			case VK_SCOPE_DEVICE_KHR:
				return "device";
			case VK_SCOPE_WORKGROUP_KHR:
				return "workgroup";
			case VK_SCOPE_SUBGROUP_KHR:
				return "subgroup";
			case VK_SCOPE_QUEUE_FAMILY_KHR:
				return "queue_family";
			default:
				return "enum" + std::to_string(static_cast<int>(s));
			}
		}

		nlohmann::json StageNames(VkShaderStageFlags flags)
		{
			nlohmann::json out = nlohmann::json::array();
			if (flags & VK_SHADER_STAGE_VERTEX_BIT)
				out.push_back("vertex");
			if (flags & VK_SHADER_STAGE_FRAGMENT_BIT)
				out.push_back("fragment");
			if (flags & VK_SHADER_STAGE_COMPUTE_BIT)
				out.push_back("compute");
			return out;
		}
	}

	void Dump(VkPhysicalDevice device)
	{
		if (!CVars::VulkanCaps.Get())
		{
			return;
		}

		VkPhysicalDeviceVulkan12Properties props12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
		VkPhysicalDeviceSubgroupSizeControlProperties sgSize{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES};
		VkPhysicalDeviceSubgroupProperties sg{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
		props12.pNext = &sgSize;
		sgSize.pNext = &sg;
		VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		props2.pNext = &props12;
		vkGetPhysicalDeviceProperties2(device, &props2);

		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> exts(extCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, exts.data());

		const bool coopExt = HasExtension(exts, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);

		// The feature chain is queried for every struct, but a struct from an extension the device does not
		// advertise stays out of the chain (querying it is invalid usage).
		VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
		VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
		VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		VkPhysicalDeviceCooperativeMatrixFeaturesKHR coopF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
		f11.pNext = &f12;
		f12.pNext = &f13;
		if (coopExt)
		{
			f13.pNext = &coopF;
		}
		VkPhysicalDeviceFeatures2 feats2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		feats2.pNext = &f11;
		vkGetPhysicalDeviceFeatures2(device, &feats2);

		nlohmann::json root;
		root["device"] = props2.properties.deviceName;
		root["vendorId"] = props2.properties.vendorID;
		root["deviceId"] = props2.properties.deviceID;
		root["apiVersion"] = std::to_string(VK_API_VERSION_MAJOR(props2.properties.apiVersion)) + "." +
		                     std::to_string(VK_API_VERSION_MINOR(props2.properties.apiVersion)) + "." +
		                     std::to_string(VK_API_VERSION_PATCH(props2.properties.apiVersion));
		root["driverName"] = props12.driverName;
		root["driverInfo"] = props12.driverInfo;
		root["driverVersionRaw"] = props2.properties.driverVersion;

		root["extensions"] = {
		    {"VK_KHR_cooperative_matrix", coopExt},
		    {"VK_KHR_shader_float16_int8", HasExtension(exts, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)},
		    {"VK_EXT_subgroup_size_control", HasExtension(exts, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)},
		    {"VK_NV_cooperative_matrix2", HasExtension(exts, "VK_NV_cooperative_matrix2")},
		};

		root["float16Int8"] = {
		    {"shaderFloat16", f12.shaderFloat16 == VK_TRUE},
		    {"shaderInt8", f12.shaderInt8 == VK_TRUE},
		    {"storageBuffer16BitAccess", f11.storageBuffer16BitAccess == VK_TRUE},
		    {"uniformAndStorageBuffer16BitAccess", f11.uniformAndStorageBuffer16BitAccess == VK_TRUE},
		    {"storageBuffer8BitAccess", f12.storageBuffer8BitAccess == VK_TRUE},
		};

		root["subgroup"] = {
		    {"size", sg.subgroupSize},
		    {"minSize", sgSize.minSubgroupSize},
		    {"maxSize", sgSize.maxSubgroupSize},
		    {"maxComputeWorkgroupSubgroups", sgSize.maxComputeWorkgroupSubgroups},
		    {"requiredSizeStages", StageNames(sgSize.requiredSubgroupSizeStages)},
		    {"subgroupSizeControl", f13.subgroupSizeControl == VK_TRUE},
		    {"computeFullSubgroups", f13.computeFullSubgroups == VK_TRUE},
		    {"supportedOperations", sg.supportedOperations},
		};

		auto& coop = root["cooperativeMatrix"] = nlohmann::json::object();
		coop["supported"] = coopExt && coopF.cooperativeMatrix == VK_TRUE;
		coop["robustBufferAccess"] = coopExt && coopF.cooperativeMatrixRobustBufferAccess == VK_TRUE;
		auto& shapes = coop["shapes"] = nlohmann::json::array();
		if (coopExt && vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)
		{
			VkPhysicalDeviceCooperativeMatrixPropertiesKHR coopP{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR};
			VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
			p2.pNext = &coopP;
			vkGetPhysicalDeviceProperties2(device, &p2);
			coop["supportedStages"] = StageNames(coopP.cooperativeMatrixSupportedStages);

			uint32_t n = 0;
			vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(device, &n, nullptr);
			std::vector<VkCooperativeMatrixPropertiesKHR> list(n, {VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR});
			vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(device, &n, list.data());
			for (const auto& s : list)
			{
				shapes.push_back({{"M", s.MSize},
				                  {"N", s.NSize},
				                  {"K", s.KSize},
				                  {"A", ComponentName(s.AType)},
				                  {"B", ComponentName(s.BType)},
				                  {"C", ComponentName(s.CType)},
				                  {"Result", ComponentName(s.ResultType)},
				                  {"saturatingAccumulation", s.saturatingAccumulation == VK_TRUE},
				                  {"scope", ScopeName(s.scope)}});
			}
		}

		SS_CORE_INFO("vulkan.caps: {} driver '{}' ({}), Vulkan {}", root["device"].get<std::string>(),
		             root["driverName"].get<std::string>(), root["driverInfo"].get<std::string>(),
		             root["apiVersion"].get<std::string>());
		SS_CORE_INFO("vulkan.caps: shaderFloat16 {}, subgroup size {} (range {}..{}, sizeControl {}), cooperative matrix {} ({} shape(s))",
		             f12.shaderFloat16 == VK_TRUE, sg.subgroupSize, sgSize.minSubgroupSize, sgSize.maxSubgroupSize,
		             f13.subgroupSizeControl == VK_TRUE, coop["supported"].get<bool>() ? "supported" : "NOT supported",
		             shapes.size());
		for (const auto& s : shapes)
		{
			SS_CORE_INFO("vulkan.caps: coopmat {}x{}x{} A={} B={} C={} R={} scope={}{}", s["M"].get<uint32_t>(),
			             s["N"].get<uint32_t>(), s["K"].get<uint32_t>(), s["A"].get<std::string>(),
			             s["B"].get<std::string>(), s["C"].get<std::string>(), s["Result"].get<std::string>(),
			             s["scope"].get<std::string>(), s["saturatingAccumulation"].get<bool>() ? " saturating" : "");
		}

		const std::filesystem::path out = EngineAssetPath(CVars::VulkanCapsPath.Get());
		if (out.has_parent_path())
		{
			std::error_code ec;
			create_directories(out.parent_path(), ec);
		}
		if (std::ofstream f(out); f)
		{
			f << root.dump(1, '\t') << "\n";
			SS_CORE_INFO("vulkan.caps: wrote {}", out.string());
		}
		else
		{
			SS_CORE_ERROR("vulkan.caps: could not write {}", out.string());
		}
	}
}
