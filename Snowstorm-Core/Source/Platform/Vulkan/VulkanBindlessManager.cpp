#include "VulkanBindlessManager.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <vector>

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
		m_RayTracing = GetVulkanContext().SupportsRayTracing();

		// 1. Create Descriptor Pool with the UpdateAfterBind flag. Two SAMPLED_IMAGE bindings live in one
		// set: binding 0 = Texture2D[], binding 1 = TextureCube[] (both SAMPLED_IMAGE; the cube-ness is in
		// the image view type). Pool must hold the sum of both arrays, plus (when RT is on) one TLAS slot.
		std::vector<VkDescriptorPoolSize> poolSizes = {
		    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES + MAX_BINDLESS_CUBES}};
		if (m_RayTracing)
		{
			poolSizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1});
		}
		VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool);

		// 2. Create Layout with Bindless Flags. ALL_GRAPHICS (was Fragment-only) so cube env maps can also
		// be sampled in other stages later; the 2D array keeps working in fragment shaders as before.
		// Binding 2 = the scene TLAS (a single acceleration structure), added only when the device supports
		// RT (#118). It's PARTIALLY_BOUND, so shaders that don't declare it are unaffected; ray-query shaders
		// (COMPUTE) read it. Kept in the same set so every pipeline that binds set 3 gets the TLAS for free.
		std::vector<VkDescriptorSetLayoutBinding> bindings = {
		    {BINDING_TEXTURE2D, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr},
		    {BINDING_TEXTURECUBE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_CUBES, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr}};

		constexpr VkDescriptorBindingFlags bindingFlag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		                                                 VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		std::vector<VkDescriptorBindingFlags> flags = {bindingFlag, bindingFlag};

		if (m_RayTracing)
		{
			bindings.push_back({BINDING_TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
			                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
			// An AS binding cannot be UPDATE_AFTER_BIND (not allowed by the spec); PARTIALLY_BOUND only.
			flags.push_back(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
		}

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
		flagsInfo.bindingCount = static_cast<uint32_t>(flags.size());
		flagsInfo.pBindingFlags = flags.data();

		VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		layoutInfo.pNext = &flagsInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
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

	void VulkanBindlessManager::WriteAccelerationStructure(const VkAccelerationStructureKHR tlas)
	{
		if (!m_RayTracing)
		{
			return; // binding 2 doesn't exist on a non-RT device
		}

		std::scoped_lock lock(m_IndexMutex);

		// A null TLAS leaves the (PARTIALLY_BOUND) slot unwritten; ray-query shaders must gate on RT being on.
		if (tlas == VK_NULL_HANDLE)
		{
			return;
		}

		VkWriteDescriptorSetAccelerationStructureKHR asWrite{
		    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
		asWrite.accelerationStructureCount = 1;
		asWrite.pAccelerationStructures = &tlas;

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.pNext = &asWrite;
		write.dstSet = m_DescriptorSet;
		write.dstBinding = BINDING_TLAS;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		write.descriptorCount = 1;

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
