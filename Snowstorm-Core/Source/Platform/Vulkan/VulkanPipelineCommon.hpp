#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/DescriptorSetLayout.hpp"
#include "Snowstorm/Render/Pipeline.hpp"

#define SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
#include <spirv_reflect.h>

#include <string>
#include <vector>

namespace Snowstorm
{
	// Steps the graphics and compute pipelines share.

	std::vector<char> ReadSpirv(const std::string& path);

	VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);

	DescriptorType FromSpvDescriptorType(SpvReflectDescriptorType type);

	std::vector<VkPushConstantRange> ToVkPushConstantRanges(const std::vector<PushConstantRangeDesc>& ranges);

	// Set i of the layout is setLayouts[i], so the array must be dense.
	VkPipelineLayout CreatePipelineLayout(VkDevice device, const std::vector<Ref<DescriptorSetLayout>>& setLayouts,
	                                      const std::vector<VkPushConstantRange>& pushConstants);
}
