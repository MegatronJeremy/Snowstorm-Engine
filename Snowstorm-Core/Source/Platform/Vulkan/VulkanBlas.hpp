#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/AccelerationStructure.hpp"

namespace Snowstorm
{
	// Vulkan bottom-level acceleration structure (#118). Wraps a VkAccelerationStructureKHR plus the
	// device-local buffer that backs it. Built synchronously in the constructor from a mesh's vertex/index
	// buffers via vkCmdBuildAccelerationStructuresKHR on ImmediateSubmit. The scratch buffer is transient
	// (freed after the build); the backing buffer + AS handle live for the object's lifetime.
	class VulkanBlas final : public BLAS
	{
	public:
		VulkanBlas(const Ref<Buffer>& vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
		           uint32_t positionOffset, const Ref<Buffer>& indexBuffer, uint32_t indexCount,
		           const std::string& debugName);
		~VulkanBlas() override;

		[[nodiscard]] uint64_t GetDeviceAddress() const override { return m_DeviceAddress; }

		[[nodiscard]] VkAccelerationStructureKHR GetHandle() const { return m_AccelStruct; }

	private:
		VkAccelerationStructureKHR m_AccelStruct = VK_NULL_HANDLE;

		VkBuffer m_Buffer = VK_NULL_HANDLE; // backing store for the AS
		VmaAllocation m_Allocation = nullptr;

		uint64_t m_DeviceAddress = 0;
	};
}
