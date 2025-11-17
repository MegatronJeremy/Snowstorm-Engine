#include "VulkanBuffer.hpp"

#include "VulkanCommon.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VulkanBuffer::VulkanBuffer(size_t size, BufferUsage usage, const void* initialData, bool hostVisible)
		: m_Usage(usage), m_Size(size), m_HostVisible(hostVisible)
	{
		VkBufferUsageFlags usageFlags = 0;
		switch (usage)
		{
		case BufferUsage::Vertex: usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			break;
		case BufferUsage::Index: usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			break;
		case BufferUsage::Uniform: usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			break;
		case BufferUsage::Storage: usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			break;
		case BufferUsage::None: break;
		}

		// TODO-:VK always add transfer dst for SetData functionality (but this can be optional for const buffer)
		usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		// TODO-VK: Enable device address if needed (using GPU addresses)
		// usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		// TODO-VK: Enable potential buffer-to-buffer copies
		// usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		VmaAllocationCreateInfo allocInfo{};
		if (hostVisible)
		{
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			// keep consistently mapped for frequently updated buffers (like uniform buffers)
			// TODO-VK -> make this more modular
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
							  VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		else
		{
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usageFlags;

		VkResult result = vmaCreateBuffer(VulkanCommon::GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &m_AllocInfo);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan buffer");

		if (initialData)
		{
			SetDataInternal(initialData, size);
		}
	}

	VulkanBuffer::~VulkanBuffer()
	{
		vmaDestroyBuffer(VulkanCommon::GetAllocator(), m_Buffer, m_Allocation);
	}

	void* VulkanBuffer::Map()
	{
		if (m_HostVisible && m_AllocInfo.pMappedData != nullptr)
		{
			// already mapped
			return m_AllocInfo.pMappedData;
		}

		void* data;
		vmaMapMemory(VulkanCommon::GetAllocator(), m_Allocation, &data);
		return data;
	}

	void VulkanBuffer::Unmap()
	{
		if (m_HostVisible && m_AllocInfo.pMappedData != nullptr)
		{
			// persistently mapped, just flush
			vmaFlushAllocation(VulkanCommon::GetAllocator(), m_Allocation, 0, VK_WHOLE_SIZE);
			return;
		}

		vmaUnmapMemory(VulkanCommon::GetAllocator(), m_Allocation);
	}

	void VulkanBuffer::SetData(const void* data, const size_t size, const size_t offset)
	{
		SetDataInternal(data, size, offset);
	}

	uint64_t VulkanBuffer::GetGPUAddress() const
	{
		// TODO-VK: Make this actually work and store usage flags
		// if (!(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT & /* storage usage flags*/))
		// {
		// 	SS_CORE_WARN("Buffer device address not enabled for this buffer);
		// 	return 0;
		// }

		VkBufferDeviceAddressInfo info{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		info.buffer = m_Buffer;

		return vkGetBufferDeviceAddress(VulkanCommon::GetVulkanDevice(), &info);
	}

	void VulkanBuffer::SetDataInternal(const void* data, const size_t size, const size_t offset) const
	{
		SS_CORE_ASSERT(data != nullptr, "Data pointer cannot be null");
		SS_CORE_ASSERT(size > 0, "Size must be greater than 0");
		SS_CORE_ASSERT(offset + size <= m_Size, "Buffer overflow: offset + size exceeds buffer size");

		if (m_HostVisible)
		{
			// persistent mapping - memory is already mapped in m_AllocInfo.pMappedData
			void* dst = m_AllocInfo.pMappedData;
			SS_CORE_ASSERT(dst != nullptr, "Buffer is not mapped");

			memcpy(static_cast<uint8_t*>(dst) + offset, data, size);

			// TODO-VK: flush if memory is not HOST_COHERENT (check this)
			vmaFlushAllocation(VulkanCommon::GetAllocator(), m_Allocation, offset, size);
		}
		else
		{
			// you'd schedule a copy via a staging buffer and command context
			// creates a staging buffer
			VkBufferCreateInfo stagingInfo{};
			stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			stagingInfo.size = size;
			stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			VmaAllocationCreateInfo stagingAllocInfo{};
			stagingAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

			VkBuffer stagingBuffer;
			VmaAllocation stagingAllocation;
			VmaAllocationInfo stagingAllocInfoResult;

			VkResult result = vmaCreateBuffer(VulkanCommon::GetAllocator(), &stagingInfo, &stagingAllocInfo,
			                                  &stagingBuffer, &stagingAllocation, &stagingAllocInfoResult);
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

			// copy data to staging
			memcpy_s(stagingAllocInfoResult.pMappedData, size, data, size);

			// TODO-VK: Copy from staging to device buffer via command buffer

			vmaDestroyBuffer(VulkanCommon::GetAllocator(), stagingBuffer, stagingAllocation);
		}
	}
}
