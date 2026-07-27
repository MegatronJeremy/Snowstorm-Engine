#include "VulkanBlas.hpp"

#include "VulkanBuffer.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		// GPU device address of a Buffer's underlying VkBuffer. The BLAS build reads vertex/index geometry by
		// address, so both must carry SHADER_DEVICE_ADDRESS (VulkanBuffer sets it on every buffer) and the
		// AS-build-input usage (set for Vertex/Index buffers when RT is on, see VulkanBuffer #118).
		uint64_t BufferAddress(const Ref<Buffer>& buffer)
		{
			return buffer->GetGPUAddress();
		}

		// A device-local buffer for AS backing / scratch. Not the engine Buffer (that always adds TRANSFER_SRC/
		// DST + is meant for app data); the AS storage + scratch want exactly ACCELERATION_STRUCTURE_STORAGE /
		// STORAGE_BUFFER + device address, nothing else. Scratch needs an alignment (see build path).
		void CreateAsBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceSize alignment,
		                    VkBuffer& outBuffer, VmaAllocation& outAllocation, const char* debugName)
		{
			VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bufferInfo.size = size;
			bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			if (debugName)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
				allocInfo.pUserData = const_cast<char*>(debugName);
			}

			VkResult result;
			if (alignment > 0)
			{
				result = vmaCreateBufferWithAlignment(GetAllocator(), &bufferInfo, &allocInfo, alignment,
				                                      &outBuffer, &outAllocation, nullptr);
			}
			else
			{
				result = vmaCreateBuffer(GetAllocator(), &bufferInfo, &allocInfo, &outBuffer, &outAllocation, nullptr);
			}
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create acceleration-structure buffer");
		}

		uint64_t RawBufferAddress(VkBuffer buffer)
		{
			VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
			info.buffer = buffer;
			return vkGetBufferDeviceAddress(GetVulkanDevice(), &info);
		}
	}

	VulkanBlas::VulkanBlas(const Ref<Buffer>& vertexBuffer, const uint32_t vertexCount, const uint32_t vertexStride,
	                       const uint32_t positionOffset, const Ref<Buffer>& indexBuffer, const uint32_t indexCount,
	                       const std::string& debugName)
	{
		const VkDevice device = GetVulkanDevice();

		// 1. Describe the triangle geometry: positions (R32G32B32 at positionOffset, stride vertexStride) +
		//    uint32 indices, both located by device address. Opaque so any-hit is skipped (shadow rays only
		//    need hit/no-hit); alpha-tested geometry would drop this flag, a deliberate later refinement.
		VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
		geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry.geometry.triangles.vertexData.deviceAddress = BufferAddress(vertexBuffer) + positionOffset;
		geometry.geometry.triangles.vertexStride = vertexStride;
		geometry.geometry.triangles.maxVertex = vertexCount - 1;
		geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geometry.geometry.triangles.indexData.deviceAddress = BufferAddress(indexBuffer);

		const uint32_t triangleCount = indexCount / 3;

		// 2. Query the sizes needed for the AS and its build scratch.
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
		    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
		    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		                                        &buildInfo, &triangleCount, &sizeInfo);

		// 3. Allocate the AS backing buffer and create the AS on it.
		CreateAsBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		               0, m_Buffer, m_Allocation, debugName.empty() ? "BLAS" : debugName.c_str());

		VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
		createInfo.buffer = m_Buffer;
		createInfo.size = sizeInfo.accelerationStructureSize;
		createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		SS_CORE_VERIFY(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &m_AccelStruct) == VK_SUCCESS,
		               "Failed to create BLAS");

		// 4. Scratch buffer, aligned to the device's minAccelerationStructureScratchOffsetAlignment.
		VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
		VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		props2.pNext = &asProps;
		vkGetPhysicalDeviceProperties2(GetVulkanPhysicalDevice(), &props2);

		VkBuffer scratchBuffer = VK_NULL_HANDLE;
		VmaAllocation scratchAllocation = nullptr;
		CreateAsBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		               asProps.minAccelerationStructureScratchOffsetAlignment, scratchBuffer, scratchAllocation,
		               "BLAS_Scratch");

		buildInfo.dstAccelerationStructure = m_AccelStruct;
		buildInfo.scratchData.deviceAddress = RawBufferAddress(scratchBuffer);

		VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
		rangeInfo.primitiveCount = triangleCount;
		const VkAccelerationStructureBuildRangeInfoKHR* pRange = &rangeInfo;

		// 5. Build on the graphics queue (ImmediateSubmit waits on a fence, so the build is complete on return
		//    and the transient scratch can be freed right after).
		ImmediateSubmit([&](const VkCommandBuffer cmd)
		                { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange); });

		vmaDestroyBuffer(GetAllocator(), scratchBuffer, scratchAllocation);

		// 6. Device address, used later as a TLAS instance's accelerationStructureReference.
		VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
		    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
		addrInfo.accelerationStructure = m_AccelStruct;
		m_DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &addrInfo);

		if (!debugName.empty())
		{
			SetVulkanObjectName(device, reinterpret_cast<uint64_t>(m_AccelStruct),
			                    VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, debugName.c_str());
		}
	}

	VulkanBlas::~VulkanBlas()
	{
		vkDeviceWaitIdle(GetVulkanDevice());
		if (m_AccelStruct != VK_NULL_HANDLE)
		{
			vkDestroyAccelerationStructureKHR(GetVulkanDevice(), m_AccelStruct, nullptr);
		}
		if (m_Buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(GetAllocator(), m_Buffer, m_Allocation);
		}
	}
}
