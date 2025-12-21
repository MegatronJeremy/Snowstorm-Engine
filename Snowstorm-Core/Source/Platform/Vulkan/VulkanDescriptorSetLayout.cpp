#include "pch.h"

#include "Platform/Vulkan/VulkanDescriptorSetLayout.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		VkDescriptorType ToVkDescriptorType(const DescriptorType type)
		{
			switch (type)
			{
			case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
			case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			}
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		}

		VkShaderStageFlags ToVkShaderStages(const ShaderStage stages)
		{
			VkShaderStageFlags flags = 0;
			if (HasStage(stages, ShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
			if (HasStage(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
			if (HasStage(stages, ShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
			return flags;
		}
	}

	VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(const DescriptorSetLayoutDesc& desc)
		: m_Desc(desc)
	{
		m_Device = VulkanCommon::GetVulkanDevice();

		SS_CORE_ASSERT(!m_Desc.Bindings.empty(), "DescriptorSetLayoutDesc must have at least one binding");

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve(m_Desc.Bindings.size());

		for (const DescriptorBindingDesc& b : m_Desc.Bindings)
		{
			SS_CORE_ASSERT(b.Count > 0, "Descriptor binding count must be > 0");

			VkDescriptorSetLayoutBinding vk{};
			vk.binding = b.Binding;
			vk.descriptorType = ToVkDescriptorType(b.Type);
			vk.descriptorCount = b.Count;
			vk.stageFlags = ToVkShaderStages(b.Visibility);
			vk.pImmutableSamplers = nullptr;

			SS_CORE_ASSERT(vk.descriptorType != VK_DESCRIPTOR_TYPE_MAX_ENUM, "Unsupported DescriptorType in set layout");
			SS_CORE_ASSERT(vk.stageFlags != 0, "Descriptor binding must be visible to at least one shader stage");

			bindings.push_back(vk);
		}

		VkDescriptorSetLayoutCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		ci.bindingCount = static_cast<uint32_t>(bindings.size());
		ci.pBindings = bindings.data();

		const VkResult res = vkCreateDescriptorSetLayout(m_Device, &ci, nullptr, &m_Layout);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkDescriptorSetLayout");
	}

	VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
	{
		if (m_Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
			m_Layout = VK_NULL_HANDLE;
		}
	}
}
