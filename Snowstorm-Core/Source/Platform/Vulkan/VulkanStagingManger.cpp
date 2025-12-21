#include "VulkanStagingManger.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"


namespace Snowstorm
{
	VulkanStagingManger::VulkanStagingManger()
	{
		CreateStagingBuffer();
		CreateCommandBuffer();
	}

	VulkanStagingManger::~VulkanStagingManger()
	{
		DestroyCommandBuffer();
		DestroyStagingBuffer();
	}

	void VulkanStagingManger::UploadBuffer(const VkBuffer dstBuffer, const void* data, size_t size, const size_t dstOffset)
	{
		std::scoped_lock lock(m_Mutex);

		SS_CORE_ASSERT(data != nullptr, "Upload data cannot be null");
		SS_CORE_ASSERT(size > 0, "Upload size must be greater than 0");

		// check if we need to flush
		if (m_StagingOffset + size > STAGING_BUFFER_SIZE)
		{
			SS_CORE_WARN("Staging buffer full, flushing pending uploads...");
			Flush();
		}

		if (size > STAGING_BUFFER_SIZE)
		{
			SS_CORE_ERROR("Upload size ({0} bytes) exceeds staging buffer size ({1} bytes)",
			              size, STAGING_BUFFER_SIZE);
			return;
		}

		// copy data to staging buffer
		memcpy(static_cast<uint8_t*>(m_StagingMappedData) + m_StagingOffset, data, size);

		// record the pending upload
		PendingUpload upload{};
		upload.dstBuffer = dstBuffer;
		upload.size = size;
		upload.dstOffset = dstOffset;
		upload.stagingOffset = m_StagingOffset;

		m_PendingUploads.emplace_back(upload);

		// advance staging offset
		m_StagingOffset += size;
		// align to 16 bytes for better performance
		m_StagingOffset = (m_StagingOffset + 15) & ~15;
	}

	// TODO-VK: see what to do with this fence here (has to come from somewhere)
	void VulkanStagingManger::Flush(const VkFence fence)
	{
		std::lock_guard lock(m_Mutex);

		if (m_PendingUploads.empty())
		{
			return;
		}

		// flush staging buffer memory
		vmaFlushAllocation(VulkanCommon::GetAllocator(), m_StagingAllocation, 0, m_StagingOffset);

		// begin command buffer
		BeginRecording();

		// record all copy commands
		for (const auto& upload : m_PendingUploads)
		{
			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = upload.stagingOffset;
			copyRegion.dstOffset = upload.dstOffset;
			copyRegion.size = upload.size;

			vkCmdCopyBuffer(m_CommandBuffer, m_StagingBuffer, upload.dstBuffer, 1, &copyRegion);
		}

		// end command buffer
		vkEndCommandBuffer(m_CommandBuffer);

		// submit
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_CommandBuffer;

		const VkQueue queue = VulkanCommon::GetGraphicsQueue();
		vkQueueSubmit(queue, 1, &submitInfo, fence);

		// reset for next batch
		ResetStaging();
	}

	void VulkanStagingManger::CreateStagingBuffer()
	{
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = STAGING_BUFFER_SIZE;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;

		const VkResult result = vmaCreateBuffer(
			VulkanCommon::GetAllocator(),
			&bufferInfo,
			&allocInfo,
			&m_StagingBuffer,
			&m_StagingAllocation,
			&m_StagingAllocationInfo
		);

		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

		m_StagingMappedData = m_StagingAllocationInfo.pMappedData;
		SS_CORE_ASSERT(m_StagingMappedData != nullptr, "Staging buffer not mapped");

		SS_CORE_INFO("Created staging buffer: {0} MB", STAGING_BUFFER_SIZE / (1024ull * 1024));
	}

	void VulkanStagingManger::DestroyStagingBuffer()
	{
		if (m_StagingBuffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(VulkanCommon::GetAllocator(), m_StagingBuffer, m_StagingAllocation);
			m_StagingBuffer = VK_NULL_HANDLE;
			m_StagingAllocation = nullptr;
			m_StagingMappedData = nullptr;
		}
	}

	void VulkanStagingManger::CreateCommandBuffer()
	{
		// create a command pool
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = VulkanCommon::GetGraphicsQueueFamilyIndex();
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		VkResult result = vkCreateCommandPool(VulkanCommon::GetVulkanDevice(), &poolInfo, nullptr, &m_CommandPool);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create command pool for staging");

		// allocate command buffer
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		result = vkAllocateCommandBuffers(VulkanCommon::GetVulkanDevice(), &allocInfo, &m_CommandBuffer);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate command buffer for staging");
	}

	void VulkanStagingManger::DestroyCommandBuffer()
	{
		if (m_CommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(VulkanCommon::GetVulkanDevice(), m_CommandPool, nullptr);
			m_CommandPool = VK_NULL_HANDLE;
			m_CommandBuffer = VK_NULL_HANDLE;
		}
	}

	void VulkanStagingManger::BeginRecording()
	{
		vkResetCommandBuffer(m_CommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		const VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to begin command buffer");

		m_IsRecording = true;
	}

	void VulkanStagingManger::ResetStaging()
	{
		m_PendingUploads.clear();
		m_StagingOffset = 0;
		m_IsRecording = false;
	}
}
