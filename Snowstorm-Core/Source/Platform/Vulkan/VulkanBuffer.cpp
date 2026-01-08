#include "VulkanBuffer.hpp"

#include "VulkanCommon.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VulkanBuffer::VulkanBuffer(const size_t size, const BufferUsage usage, const void* initialData, const bool hostVisible, const std::string& debugName)
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

		// For device-local buffers - TRANSFER_DST for staging uploads
		// HostVisible - we map directly
		if (!hostVisible)
		{
			usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		}

		// Support buffer-to-buffer copies by default
		usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		// Enable device address usage by default - buffers can be used with GPU pointers
		usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		m_UsageFlags = usageFlags;

		VmaAllocationCreateInfo allocInfo{};
		if (hostVisible)
		{
			// Frequently updated buffers (e.g., uniform) can safely stay mapped
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		else
		{
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		}

		if (!debugName.empty())
		{
			allocInfo.flags |= VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
			allocInfo.pUserData = (void*)debugName.c_str();
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usageFlags;

		VkResult result = vmaCreateBuffer(
			GetAllocator(),
			&bufferInfo,
			&allocInfo,
			&m_Buffer,
			&m_Allocation,
			&m_AllocInfo
		);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan buffer");

		// Query memory properties so we can decide whether flushing is needed
		vmaGetAllocationMemoryProperties(
			GetAllocator(),
			m_Allocation,
			&m_MemoryProperties
		);

		if (initialData)
		{
			SetDataInternal(initialData, size);
		}
	}

	VulkanBuffer::~VulkanBuffer()
	{
		vkDeviceWaitIdle(GetVulkanDevice()); // TODO this is probably really bad practice, having it here and in other places 
		vmaDestroyBuffer(GetAllocator(), m_Buffer, m_Allocation);
	}

	void* VulkanBuffer::Map()
	{
		if (m_HostVisible && m_AllocInfo.pMappedData != nullptr)
		{
			return m_AllocInfo.pMappedData;
		}

		void* data;
		VkResult result = vmaMapMemory(GetAllocator(), m_Allocation, &data);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to map Vulkan buffer memory");

		return data;
	}

	void VulkanBuffer::Unmap()
	{
		if (m_HostVisible && m_AllocInfo.pMappedData != nullptr)
		{
			// persistently mapped; only flush if memory is not HOST_COHERENT
			if ((m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
			{
				vmaFlushAllocation(GetAllocator(), m_Allocation, 0, VK_WHOLE_SIZE);
			}
			return;
		}

		vmaUnmapMemory(GetAllocator(), m_Allocation);
	}

	void VulkanBuffer::SetData(const void* data, const size_t size, const size_t offset)
	{
		SetDataInternal(data, size, offset);
	}

	uint64_t VulkanBuffer::GetGPUAddress() const
	{
		if ((m_UsageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0)
		{
			SS_CORE_WARN("Buffer GPU address not enabled for this buffer");
			return 0;
		}

		VkBufferDeviceAddressInfo info{};
		info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		info.buffer = m_Buffer;

		return vkGetBufferDeviceAddress(GetVulkanDevice(), &info);
	}

	void VulkanBuffer::SetDataInternal(const void* data, const size_t size, const size_t offset) const
	{
		SS_CORE_ASSERT(data != nullptr, "Data pointer cannot be null");
		SS_CORE_ASSERT(size > 0, "Size must be greater than 0");
		SS_CORE_ASSERT(offset + size <= m_Size, "Buffer overflow: offset + size exceeds buffer size");

		if (m_HostVisible)
		{
			// persistent mapping - memory is already mapped in m_AllocInfo.pMappedData (by creation flags)
			void* dst = m_AllocInfo.pMappedData;
			SS_CORE_ASSERT(dst != nullptr, "Buffer is not mapped");

			memcpy(static_cast<uint8_t*>(dst) + offset, data, size);

			// Only flush if the memory is not HOST_COHERENT
			if ((m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
			{
				vmaFlushAllocation(GetAllocator(), m_Allocation, offset, size);
			}
		}
		else
		{
			// Device-local memory: upload via a staging buffer and a one-time command buffer

			// Create a staging buffer
			VkBufferCreateInfo stagingInfo{};
			stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			stagingInfo.size = size;
			stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			VmaAllocationCreateInfo stagingAllocInfo{};
			stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT |
			VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;

			VkBuffer stagingBuffer = VK_NULL_HANDLE;
			VmaAllocation stagingAllocation = nullptr;
			VmaAllocationInfo stagingAllocInfoResult{};

			static uint32_t debugCounter = 0;
			auto name = std::string("VulkanBufferStaging ") + std::to_string(debugCounter++);
			stagingAllocInfo.pUserData = (void*)name.c_str();

			VkResult result = vmaCreateBuffer(
				GetAllocator(),
				&stagingInfo,
				&stagingAllocInfo,
				&stagingBuffer,
				&stagingAllocation,
				&stagingAllocInfoResult
			);
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

			// Copy data to staging
			memcpy(stagingAllocInfoResult.pMappedData, data, size);

			// Flush staging if needed
			VkMemoryPropertyFlags stagingProperties = 0;
			vmaGetAllocationMemoryProperties(
				GetAllocator(),
				stagingAllocation,
				&stagingProperties
			);

			if ((stagingProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
			{
				vmaFlushAllocation(GetAllocator(), stagingAllocation, 0, size);
			}

			// Record and submit copy 
			ImmediateSubmit([&](const VkCommandBuffer cmd)
			{
				VkBufferCopy copyRegion{};
				copyRegion.srcOffset = 0;
				copyRegion.dstOffset = offset;
				copyRegion.size = size;

				vkCmdCopyBuffer(cmd, stagingBuffer, m_Buffer, 1, &copyRegion);

				// Ensure the transfer is finished and visible to the vertex input stage
				VkMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
				barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

				VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
				dep.memoryBarrierCount = 1;
				dep.pMemoryBarriers = &barrier;

				vkCmdPipelineBarrier2(cmd, &dep);
			});

			vmaDestroyBuffer(GetAllocator(), stagingBuffer, stagingAllocation);
		}
	}
}
