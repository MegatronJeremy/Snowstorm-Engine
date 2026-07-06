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

		// 1. Create Descriptor Pool with the UpdateAfterBind flag. Two SAMPLED_IMAGE bindings live in one
		// set: binding 0 = Texture2D[], binding 1 = TextureCube[] (both SAMPLED_IMAGE; the cube-ness is in
		// the image view type). Pool must hold the sum of both arrays.
		VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES + MAX_BINDLESS_CUBES};
		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool);

		// 2. Create Layout with Bindless Flags. ALL_GRAPHICS (was Fragment-only) so cube env maps can also
		// be sampled in other stages later; the 2D array keeps working in fragment shaders as before.
		VkDescriptorSetLayoutBinding bindings[2]{};
		bindings[0].binding = BINDING_TEXTURE2D;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[0].descriptorCount = MAX_BINDLESS_TEXTURES;
		bindings[0].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
		bindings[1].binding = BINDING_TEXTURECUBE;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[1].descriptorCount = MAX_BINDLESS_CUBES;
		bindings[1].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

		constexpr VkDescriptorBindingFlags bindingFlag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		                                                 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		const VkDescriptorBindingFlags flags[2] = {bindingFlag, bindingFlag};
		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
		flagsInfo.bindingCount = 2;
		flagsInfo.pBindingFlags = flags;

		VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		layoutInfo.pNext = &flagsInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.bindingCount = 2;
		layoutInfo.pBindings = bindings;
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

		// Indices are handed out monotonically with no recycling yet, so guard the array bound.
		// Without this, an overflow writes past the descriptor array (silent corruption in release).
		SS_CORE_ASSERT(m_NextFreeIndex < MAX_BINDLESS_TEXTURES, "Bindless texture array is full");
		if (m_NextFreeIndex >= MAX_BINDLESS_TEXTURES)
		{
			SS_CORE_ERROR("VulkanBindlessManager: out of bindless slots (max {0}); reusing slot 0", MAX_BINDLESS_TEXTURES);
			return 0;
		}

		const uint32_t index = m_NextFreeIndex++;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_DescriptorSet;
		write.dstBinding = BINDING_TEXTURE2D;
		write.dstArrayElement = index;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
		return index;
	}

	void VulkanBindlessManager::WriteTexture(const uint32_t index, const VkImageView imageView)
	{
		std::scoped_lock lock(m_IndexMutex);

		// Repoint an EXISTING slot (must have been handed out by RegisterTexture). Does not touch
		// m_NextFreeIndex — no new slot is allocated.
		SS_CORE_ASSERT(index < m_NextFreeIndex, "WriteTexture: slot {} was never registered", index);

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_DescriptorSet;
		write.dstBinding = BINDING_TEXTURE2D;
		write.dstArrayElement = index;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
	}

	uint32_t VulkanBindlessManager::RegisterCube(const VkImageView imageView)
	{
		std::scoped_lock lock(m_IndexMutex);

		SS_CORE_ASSERT(m_NextFreeCubeIndex < MAX_BINDLESS_CUBES, "Bindless cube array is full");
		if (m_NextFreeCubeIndex >= MAX_BINDLESS_CUBES)
		{
			SS_CORE_ERROR("VulkanBindlessManager: out of bindless cube slots (max {0}); reusing slot 0", MAX_BINDLESS_CUBES);
			return 0;
		}

		const uint32_t index = m_NextFreeCubeIndex++;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageView = imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_DescriptorSet;
		write.dstBinding = BINDING_TEXTURECUBE;
		write.dstArrayElement = index;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
		return index;
	}

	void VulkanBindlessManager::Shutdown()
	{
		if (m_Device == VK_NULL_HANDLE)
			return;

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
