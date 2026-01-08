#include "VulkanBindlessManager.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VulkanBindlessManager& VulkanBindlessManager::Get()
	{
		static VulkanBindlessManager instance;
		return instance;
	}

	void VulkanBindlessManager::Init()
	{
		m_Device = GetVulkanDevice();

		// 1. Create Descriptor Pool with the UpdateAfterBind flag
		VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES};
		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool);

		// 2. Create Layout with Bindless Flags
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		binding.descriptorCount = MAX_BINDLESS_TEXTURES;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
		flagsInfo.bindingCount = 1;
		flagsInfo.pBindingFlags = &flags;

		VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		layoutInfo.pNext = &flagsInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;
		vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_Layout);

		// 3. Allocate the one and only Global Set
		VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		allocInfo.descriptorPool = m_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_Layout;
		vkAllocateDescriptorSets(m_Device, &allocInfo, &m_DescriptorSet);
	}

	uint32_t VulkanBindlessManager::RegisterTexture(const VkImageView imageView)
	{
		std::scoped_lock lock(m_IndexMutex);
		const uint32_t index = m_NextFreeIndex++;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_DescriptorSet;
		write.dstBinding = 0;
		write.dstArrayElement = index;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
		return index;
	}

	void VulkanBindlessManager::Shutdown() 
	{
		if (m_Device == VK_NULL_HANDLE) return;

		vkDeviceWaitIdle(m_Device);

		if (m_Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
			m_Layout = VK_NULL_HANDLE;
		}

		if (m_DescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
			m_DescriptorPool = VK_NULL_HANDLE;
		}
		
		// Mark device as null so we don't try to use it again
		m_Device = VK_NULL_HANDLE;
	}
}
