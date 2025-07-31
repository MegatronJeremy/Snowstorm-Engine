#include "VulkanBuffer.hpp"

#include "VulkanCommon.hpp"

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
		usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = hostVisible
			                  ? VMA_MEMORY_USAGE_CPU_TO_GPU
			                  : VMA_MEMORY_USAGE_GPU_ONLY;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usageFlags;

		vmaCreateBuffer(VulkanCommon::GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &m_AllocInfo);

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
		void* data;
		vmaMapMemory(VulkanCommon::GetAllocator(), m_Allocation, &data);
		return data;
	}

	void VulkanBuffer::Unmap()
	{
		vmaUnmapMemory(VulkanCommon::GetAllocator(), m_Allocation);
	}

	void VulkanBuffer::SetData(const void* data, const size_t size, const size_t offset)
	{
		SetDataInternal(data, size, offset);
	}

	uint64_t VulkanBuffer::GetGPUAddress() const
	{
		// Optional: If you use VK_KHR_buffer_device_address
		return 0;
	}

	void VulkanBuffer::SetDataInternal(const void* data, const size_t size, const size_t offset) const
	{
		if (m_HostVisible)
		{
			void* dst;
			vmaMapMemory(VulkanCommon::GetAllocator(), m_Allocation, &dst);
			memcpy(static_cast<uint8_t*>(dst) + offset, data, size);
			vmaUnmapMemory(VulkanCommon::GetAllocator(), m_Allocation);
		}
		else
		{
			// You'd schedule a copy via a staging buffer and command context
		}
	}
}
