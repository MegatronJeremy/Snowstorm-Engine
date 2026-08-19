#include "VulkanTlas.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <cstring>

namespace Snowstorm
{
	namespace
	{
		uint64_t RawBufferAddress(VkBuffer buffer)
		{
			VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
			info.buffer = buffer;
			return vkGetBufferDeviceAddress(GetVulkanDevice(), &info);
		}

		// Create/replace a buffer when the needed size exceeds current capacity. Grows only (keeps the
		// allocation stable across frames when the instance count is steady). hostVisible => persistently
		// mapped for the instance array; device-local for AS storage/scratch.
		void EnsureBuffer(VkDeviceSize needed, VkBufferUsageFlags usage, bool hostVisible, VkDeviceSize alignment,
		                  VkBuffer& buffer, VmaAllocation& allocation, VkDeviceSize& capacity, const char* debugName)
		{
			if (needed <= capacity && buffer != VK_NULL_HANDLE)
			{
				return;
			}
			if (buffer != VK_NULL_HANDLE)
			{
				vmaDestroyBuffer(GetAllocator(), buffer, allocation);
				buffer = VK_NULL_HANDLE;
				allocation = nullptr;
			}

			VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bufferInfo.size = needed;
			bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = hostVisible ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			if (hostVisible)
			{
				allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			if (debugName)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
				allocInfo.pUserData = const_cast<char*>(debugName);
			}

			VkResult result;
			if (alignment > 0)
			{
				result = vmaCreateBufferWithAlignment(GetAllocator(), &bufferInfo, &allocInfo, alignment, &buffer, &allocation, nullptr);
			}
			else
			{
				result = vmaCreateBuffer(GetAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
			}
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create TLAS buffer");
			capacity = needed;
		}
	}

	VulkanTlas::VulkanTlas(const std::string& debugName)
	    : m_DebugName(debugName)
	{
	}

	VulkanTlas::~VulkanTlas()
	{
		vkDeviceWaitIdle(GetVulkanDevice());
		Destroy();
		if (m_InstanceBuffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(GetAllocator(), m_InstanceBuffer, m_InstanceAllocation);
		}
		if (m_ScratchBuffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(GetAllocator(), m_ScratchBuffer, m_ScratchAllocation);
		}
	}

	void VulkanTlas::Destroy()
	{
		if (m_AccelStruct != VK_NULL_HANDLE)
		{
			vkDestroyAccelerationStructureKHR(GetVulkanDevice(), m_AccelStruct, nullptr);
			m_AccelStruct = VK_NULL_HANDLE;
		}
		if (m_AsBuffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(GetAllocator(), m_AsBuffer, m_AsAllocation);
			m_AsBuffer = VK_NULL_HANDLE;
			m_AsAllocation = nullptr;
		}
	}

	void VulkanTlas::Build(const std::vector<TLASInstance>& instances)
	{
		const VkDevice device = GetVulkanDevice();
		const uint32_t count = static_cast<uint32_t>(instances.size());
		m_InstanceCount = count;

		// The previous AS/backing must be torn down before we build a new one (rebuild-each-call). The GPU
		// isn't using it here — TlasBuildSystem runs in PreRender, before this frame's ray-query pass, and the
		// prior build's ImmediateSubmit already fenced. A device wait keeps it simple and safe at this scale.
		vkDeviceWaitIdle(device);
		Destroy();

		// 1. Fill the host-visible instance array. Vulkan wants a row-major 3x4 (transform[row][col]); glm is
		//    column-major, so transpose the upper 3x4 of each world matrix.
		const VkDeviceSize instancesBytes = std::max<VkDeviceSize>(1, count) * sizeof(VkAccelerationStructureInstanceKHR);
		EnsureBuffer(instancesBytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, 0,
		             m_InstanceBuffer, m_InstanceAllocation, m_InstanceCapacity, "TLAS_Instances");

		if (count > 0)
		{
			VmaAllocationInfo allocInfo{};
			vmaGetAllocationInfo(GetAllocator(), m_InstanceAllocation, &allocInfo);
			auto* dst = static_cast<VkAccelerationStructureInstanceKHR*>(allocInfo.pMappedData);
			for (uint32_t i = 0; i < count; ++i)
			{
				const glm::mat4 t = glm::transpose(instances[i].Transform);
				VkAccelerationStructureInstanceKHR inst{};
				std::memcpy(&inst.transform, &t, sizeof(VkTransformMatrixKHR)); // first 3 rows = 12 floats
				inst.instanceCustomIndex = i;
				inst.mask = 0xFF;
				inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				// Masked (glTF MASK) instances must be non-opaque so RayQuery surfaces their triangles as
				// candidates for the any-hit alpha test; this instance bit overrides the geometry's OPAQUE bit
				// below. Without it cutout foliage renders solid in every RT pass (#151).
				if (instances[i].ForceNonOpaque)
				{
					inst.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
				}
				inst.accelerationStructureReference = instances[i].BlasAddress;
				dst[i] = inst;
			}
			vmaFlushAllocation(GetAllocator(), m_InstanceAllocation, 0, count * sizeof(VkAccelerationStructureInstanceKHR));
		}

		// 2. Geometry = the instance array by device address.
		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		geometry.geometry.instances.arrayOfPointers = VK_FALSE;
		geometry.geometry.instances.data.deviceAddress = RawBufferAddress(m_InstanceBuffer);

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
		    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		// 3. Sizes, then AS storage + AS handle.
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
		    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		                                        &buildInfo, &count, &sizeInfo);

		// AS storage is freed+recreated every Build (Destroy() nulls m_AsBuffer above), so a throwaway
		// zero capacity forces a fresh allocation each call.
		VkDeviceSize asCapacity = 0;
		EnsureBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		             false, 0, m_AsBuffer, m_AsAllocation, asCapacity,
		             m_DebugName.empty() ? "TLAS" : m_DebugName.c_str());

		VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
		createInfo.buffer = m_AsBuffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		SS_CORE_VERIFY(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &m_AccelStruct) == VK_SUCCESS,
		               "Failed to create TLAS");

		// 4. Scratch, aligned to the device requirement.
		VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
		VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		props2.pNext = &asProps;
		vkGetPhysicalDeviceProperties2(GetVulkanPhysicalDevice(), &props2);

		EnsureBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
		             asProps.minAccelerationStructureScratchOffsetAlignment, m_ScratchBuffer, m_ScratchAllocation,
		             m_ScratchCapacity, "TLAS_Scratch");

		buildInfo.dstAccelerationStructure = m_AccelStruct;
		buildInfo.scratchData.deviceAddress = RawBufferAddress(m_ScratchBuffer);

		VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
		rangeInfo.primitiveCount = count;
		const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;

		ImmediateSubmit([&](const VkCommandBuffer cmd)
		                { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange); });

		if (!m_DebugName.empty())
		{
			SetVulkanObjectName(device, reinterpret_cast<uint64_t>(m_AccelStruct),
			                    VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, m_DebugName.c_str());
		}
	}
}
