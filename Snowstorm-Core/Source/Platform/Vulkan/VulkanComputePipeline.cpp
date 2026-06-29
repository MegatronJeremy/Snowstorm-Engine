#include "VulkanComputePipeline.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>
#include <map>

#define SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
#include <spirv_reflect.h>

#include "VulkanDescriptorSetLayout.hpp"

namespace Snowstorm
{
	namespace
	{
		std::vector<char> ReadFile(const std::string& filename)
		{
			std::ifstream file(filename, std::ios::ate | std::ios::binary);
			SS_CORE_ASSERT(file.is_open(), "Failed to open compute SPIR-V file!");
			const size_t fileSize = file.tellg();
			std::vector<char> buffer(fileSize);
			file.seekg(0);
			file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
			return buffer;
		}

		VkShaderModule CreateShaderModule(const VkDevice device, const std::vector<char>& code)
		{
			VkShaderModuleCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			ci.codeSize = code.size();
			ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

			VkShaderModule mod = VK_NULL_HANDLE;
			SS_CORE_ASSERT(vkCreateShaderModule(device, &ci, nullptr, &mod) == VK_SUCCESS, "Failed to create compute shader module");
			return mod;
		}

		DescriptorType FromSpvDescriptorType(const SpvReflectDescriptorType t)
		{
			switch (t)
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
				SS_CORE_ASSERT(false, "Unsupported SPIR-V descriptor type in compute shader");
				return DescriptorType::UniformBuffer;
			}
		}
	}

	VulkanComputePipeline::VulkanComputePipeline(PipelineDesc desc)
	    : m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Desc.Type == PipelineType::Compute, "VulkanComputePipeline requires PipelineType::Compute");
		SS_CORE_ASSERT(m_Desc.Shader, "PipelineDesc.Shader must be set");

		m_Device = GetVulkanDevice();

		const std::string compPath = m_Desc.Shader->GetCompiledPath(ShaderStageKind::Compute);
		SS_CORE_ASSERT(!compPath.empty(), "Compute pipeline shader has no compiled compute SPIR-V (missing #type compute?)");

		const std::vector<char> code = ReadFile(compPath);
		const VkShaderModule module = CreateShaderModule(m_Device, code);

		// --- Reflect descriptor set layouts from the SPIR-V ---
		SpvReflectShaderModule reflect;
		SS_CORE_ASSERT(spvReflectCreateShaderModule(code.size(), code.data(), &reflect) == SPV_REFLECT_RESULT_SUCCESS,
		               "Failed to reflect compute SPIR-V");

		uint32_t setCount = 0;
		spvReflectEnumerateDescriptorSets(&reflect, &setCount, nullptr);
		std::vector<SpvReflectDescriptorSet*> reflectSets(setCount);
		spvReflectEnumerateDescriptorSets(&reflect, &setCount, reflectSets.data());

		// Build one DescriptorSetLayout per reflected set, ordered by set index (pipeline layout needs a
		// dense, ascending array; compute shaders here use contiguous sets starting at 0).
		std::map<uint32_t, SpvReflectDescriptorSet*> orderedSets;
		for (SpvReflectDescriptorSet* s : reflectSets)
		{
			orderedSets[s->set] = s;
		}

		for (const auto& [setIndex, set] : orderedSets)
		{
			DescriptorSetLayoutDesc layoutDesc{};
			layoutDesc.SetIndex = setIndex;
			layoutDesc.DebugName = m_Desc.DebugName + "_Set" + std::to_string(setIndex);

			for (uint32_t b = 0; b < set->binding_count; ++b)
			{
				const SpvReflectDescriptorBinding* rb = set->bindings[b];
				DescriptorBindingDesc binding{};
				binding.Binding = rb->binding;
				binding.Type = FromSpvDescriptorType(rb->descriptor_type);
				binding.Count = rb->count;
				binding.Visibility = ShaderStage::Compute;
				binding.DebugName = rb->name ? rb->name : "";
				layoutDesc.Bindings.push_back(binding);
			}

			m_SetLayouts.push_back(DescriptorSetLayout::Create(layoutDesc));
			SS_CORE_ASSERT(m_SetLayouts.back(), "Failed to create compute DescriptorSetLayout");
		}

		spvReflectDestroyShaderModule(&reflect);

		// --- Push constants (from PipelineDesc, same as graphics) ---
		m_VkPushConstantRanges.reserve(m_Desc.PushConstants.size());
		for (const PushConstantRangeDesc& r : m_Desc.PushConstants)
		{
			VkPushConstantRange vkRange{};
			vkRange.offset = r.Offset;
			vkRange.size = r.Size;
			vkRange.stageFlags = ToVkShaderStages(r.Stages);
			SS_CORE_ASSERT(vkRange.stageFlags != 0, "PushConstantRangeDesc.Stages must not be None");
			m_VkPushConstantRanges.push_back(vkRange);
		}

		// --- Pipeline layout ---
		std::vector<VkDescriptorSetLayout> vkSetLayouts;
		vkSetLayouts.reserve(m_SetLayouts.size());
		for (const auto& s : m_SetLayouts)
		{
			vkSetLayouts.push_back(std::static_pointer_cast<VulkanDescriptorSetLayout>(s)->GetHandle());
		}

		VkPipelineLayoutCreateInfo layoutCI{};
		layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutCI.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
		layoutCI.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
		layoutCI.pushConstantRangeCount = static_cast<uint32_t>(m_VkPushConstantRanges.size());
		layoutCI.pPushConstantRanges = m_VkPushConstantRanges.empty() ? nullptr : m_VkPushConstantRanges.data();

		SS_CORE_ASSERT(vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_PipelineLayout) == VK_SUCCESS,
		               "Failed to create compute VkPipelineLayout");

		// --- Compute pipeline ---
		VkPipelineShaderStageCreateInfo stage{};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = module;
		stage.pName = "main";

		VkComputePipelineCreateInfo pipeCI{};
		pipeCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeCI.stage = stage;
		pipeCI.layout = m_PipelineLayout;

		SS_CORE_ASSERT(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &m_Pipeline) == VK_SUCCESS,
		               "Failed to create Vulkan compute pipeline");

		vkDestroyShaderModule(m_Device, module, nullptr);
	}

	VulkanComputePipeline::~VulkanComputePipeline()
	{
		if (m_Pipeline != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
			m_Pipeline = VK_NULL_HANDLE;
		}
		if (m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
		}
	}

	VkShaderStageFlags VulkanComputePipeline::GetVkPushConstantStagesFor(const uint32_t offset, const uint32_t size) const
	{
		const uint32_t end = offset + size;
		for (const VkPushConstantRange& r : m_VkPushConstantRanges)
		{
			if (offset >= r.offset && end <= r.offset + r.size)
			{
				return r.stageFlags;
			}
		}
		return 0;
	}
}
