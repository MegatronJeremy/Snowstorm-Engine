#include "Platform/Vulkan/VulkanDescriptorSetLayout.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(DescriptorSetLayoutDesc desc)
		: m_Desc(std::move(desc))
	{
		m_Device = GetVulkanDevice();

		SS_CORE_ASSERT(!m_Desc.Bindings.empty(), "DescriptorSetLayoutDesc must have at least one binding");

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkDescriptorBindingFlags> bindingFlags;
		bindings.reserve(m_Desc.Bindings.size());
		bindingFlags.reserve(m_Desc.Bindings.size());

		bool hasBindless = false;

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

			VkDescriptorBindingFlags flags = 0;
			if (b.IsBindless)
			{
				flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
				flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
				flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
				hasBindless = true;
			}
			bindingFlags.push_back(flags);
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
		flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		flagsInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		ci.bindingCount = static_cast<uint32_t>(bindings.size());
		ci.pBindings = bindings.data();

		if (hasBindless)
		{
			ci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
			ci.pNext = &flagsInfo;
		}

		const VkResult res = vkCreateDescriptorSetLayout(m_Device, &ci, nullptr, &m_Layout);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkDescriptorSetLayout");
	}

	VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(void* internalHandle)
		: m_Layout(static_cast<VkDescriptorSetLayout>(internalHandle))
		  , m_IsLayoutOwner(false)
	{
		m_Device = GetVulkanDevice();
		m_Desc.DebugName = "External_Layout_Wrapper";
	}

	VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
	{
		if (m_IsLayoutOwner && m_Layout != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
			m_Layout = VK_NULL_HANDLE;
		}
	}
}
