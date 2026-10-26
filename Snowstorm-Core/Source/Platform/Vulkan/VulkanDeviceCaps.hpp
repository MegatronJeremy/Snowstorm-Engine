#pragma once

#include <volk.h>

// Driver-reported capabilities that decide whether a planned GPU technique can run on this adapter,
// written to vulkan.caps.path as JSON and logged. Today: VK_KHR_cooperative_matrix (every supported
// M/N/K/type/scope combination), VK_KHR_shader_float16_int8 and VK_KHR_subgroup_size_control (the
// wave32/wave64 range). Queried from the physical device without enabling any extension, so a normal
// run is unaffected and the dump reports what the driver offers, not what the engine enabled.
namespace Snowstorm::VulkanDeviceCaps
{
	// No-op unless vulkan.caps is set. Call once after the physical device is chosen.
	void Dump(VkPhysicalDevice device);
}
