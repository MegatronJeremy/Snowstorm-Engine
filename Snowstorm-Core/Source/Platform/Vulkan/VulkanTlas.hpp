#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/AccelerationStructure.hpp"

namespace Snowstorm
{
	// Vulkan top-level acceleration structure (#118). Owns a VkAccelerationStructureKHR plus the buffers that
	// back it: the AS storage, the VkAccelerationStructureInstanceKHR array (host-visible, rewritten each
	// build), and a scratch buffer. Rebuilds fully on each Build() — cheap at this scene scale. The AS handle
	// is written into the bindless set (binding 2) so ray-query shaders trace it.
	class VulkanTlas final : public TLAS
	{
	public:
		explicit VulkanTlas(const std::string& debugName);
		~VulkanTlas() override;

		void Build(const std::vector<TLASInstance>& instances) override;

		[[nodiscard]] uint32_t GetInstanceCount() const override { return m_InstanceCount; }

		[[nodiscard]] VkAccelerationStructureKHR GetHandle() const { return m_AccelStruct; }

	private:
		void Destroy();

		std::string m_DebugName;

		VkAccelerationStructureKHR m_AccelStruct = VK_NULL_HANDLE;

		VkBuffer m_AsBuffer = VK_NULL_HANDLE; // AS storage
		VmaAllocation m_AsAllocation = nullptr;

		VkBuffer m_InstanceBuffer = VK_NULL_HANDLE; // host-visible instance array
		VmaAllocation m_InstanceAllocation = nullptr;
		VkDeviceSize m_InstanceCapacity = 0; // bytes currently allocated for the instance array

		VkBuffer m_ScratchBuffer = VK_NULL_HANDLE; // build scratch
		VmaAllocation m_ScratchAllocation = nullptr;
		VkDeviceSize m_ScratchCapacity = 0;

		uint32_t m_InstanceCount = 0;
	};
}
