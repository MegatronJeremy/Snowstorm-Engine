#include "VulkanPipelineCommon.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "VulkanDescriptorSetLayout.hpp"

#include <fstream>

namespace Snowstorm
{
	std::vector<char> ReadSpirv(const std::string& path)
	{
		std::ifstream file(path, std::ios::ate | std::ios::binary);
		SS_CORE_ASSERT(file.is_open(), "Failed to open SPIR-V file!");
		const size_t fileSize = file.tellg();
		std::vector<char> buffer(fileSize);
		file.seekg(0);
		file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
		return buffer;
	}

	VkShaderModule CreateShaderModule(const VkDevice device, const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		ci.codeSize = code.size();
		ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
		VkShaderModule module = VK_NULL_HANDLE;
		SS_CORE_VERIFY(vkCreateShaderModule(device, &ci, nullptr, &module) == VK_SUCCESS, "Failed to create shader module");
		return module;
	}

	DescriptorType FromSpvDescriptorType(const SpvReflectDescriptorType type)
	{
		switch (type)
		{
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			return DescriptorType::UniformBuffer;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			return DescriptorType::StorageBuffer;
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			return DescriptorType::SampledImage;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			return DescriptorType::StorageImage;
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
			return DescriptorType::Sampler;
		case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			return DescriptorType::CombinedImageSampler;
		default:
			SS_CORE_ASSERT(false, "Unsupported SPIR-V descriptor type");
			return DescriptorType::UniformBuffer;
		}
	}

	std::vector<VkPushConstantRange> ToVkPushConstantRanges(const std::vector<PushConstantRangeDesc>& ranges)
	{
		std::vector<VkPushConstantRange> out;
		out.reserve(ranges.size());
		for (const PushConstantRangeDesc& r : ranges)
		{
			const VkPushConstantRange range{ToVkShaderStages(r.Stages), r.Offset, r.Size};
			SS_CORE_ASSERT(range.stageFlags != 0, "PushConstantRangeDesc.Stages must not be None");
			out.push_back(range);
		}
		return out;
	}

	VkPipelineLayout CreatePipelineLayout(const VkDevice device, const std::vector<Ref<DescriptorSetLayout>>& setLayouts,
	                                      const std::vector<VkPushConstantRange>& pushConstants)
	{
		std::vector<VkDescriptorSetLayout> handles;
		handles.reserve(setLayouts.size());
		for (const Ref<DescriptorSetLayout>& layout : setLayouts)
		{
			SS_CORE_ASSERT(layout, "Null DescriptorSetLayout in pipeline");
			handles.push_back(std::static_pointer_cast<VulkanDescriptorSetLayout>(layout)->GetHandle());
		}

		VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		ci.setLayoutCount = static_cast<uint32_t>(handles.size());
		ci.pSetLayouts = handles.empty() ? nullptr : handles.data();
		ci.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
		ci.pPushConstantRanges = pushConstants.empty() ? nullptr : pushConstants.data();
		VkPipelineLayout layout = VK_NULL_HANDLE;
		SS_CORE_VERIFY(vkCreatePipelineLayout(device, &ci, nullptr, &layout) == VK_SUCCESS, "Failed to create VkPipelineLayout");
		return layout;
	}
}
