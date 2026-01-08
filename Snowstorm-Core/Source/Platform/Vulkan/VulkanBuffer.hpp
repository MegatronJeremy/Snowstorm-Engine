#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/Buffer.hpp"

namespace Snowstorm
{
	class VulkanBuffer : public Buffer
	{
	public:
		VulkanBuffer(size_t size, BufferUsage usage, const void* initialData, bool hostVisible, const std::string& debugName);
		~VulkanBuffer() override;

		void* Map() override;
		void Unmap() override;
		void SetData(const void* data, size_t size, size_t offset = 0) override;

		uint64_t GetGPUAddress() const override;
		size_t GetSize() const override { return m_Size; }
		BufferUsage GetUsage() const override { return m_Usage; }

		VkBuffer GetHandle() const { return m_Buffer; }

	private:
		void SetDataInternal(const void* data, size_t size, size_t offset = 0) const;

		BufferUsage m_Usage;
		size_t m_Size;
		bool m_HostVisible;

		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = nullptr;
		VmaAllocationInfo m_AllocInfo{};

		VkBufferUsageFlags m_UsageFlags = 0;
		VkMemoryPropertyFlags m_MemoryProperties = 0;
	};
}
