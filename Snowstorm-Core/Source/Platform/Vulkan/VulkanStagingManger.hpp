#pragma once

#include <mutex>

#include "VulkanCommon.hpp"

namespace Snowstorm
{
	class VulkanStagingManger
	{
	public:
		VulkanStagingManger();
		~VulkanStagingManger();

		// schedule a buffer upload (non-blocking)
		void UploadBuffer(VkBuffer dstBuffer, const void* data, size_t size, size_t dstOffset = 0);

		// submit all pending uploads and wait for completion (async with fence)
		void Flush(VkFence fence = VK_NULL_HANDLE);

	private:
		struct PendingUpload
		{
			VkBuffer dstBuffer;
			size_t size;
			size_t dstOffset;
			size_t stagingOffset; // offset within the stagin buffer
		};

		static constexpr size_t STAGING_BUFFER_SIZE = 64ull * 1024 * 1024; // 64 MB

		// staging buffer (large, reusable)
		VkBuffer m_StagingBuffer = VK_NULL_HANDLE;
		VmaAllocation m_StagingAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo m_StagingAllocationInfo{};
		void* m_StagingMappedData = nullptr;
		size_t m_StagingOffset = 0; // current write position

		// command buffer for transfers
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

		// pending uploads
		std::vector<PendingUpload> m_PendingUploads;
		std::mutex m_Mutex; // thread safety

		bool m_IsRecording = false;

		void CreateStagingBuffer();
		void DestroyStagingBuffer();
		void CreateCommandBuffer();
		void DestroyCommandBuffer();
		void BeginRecording();
		void ResetStaging();
	};
}
