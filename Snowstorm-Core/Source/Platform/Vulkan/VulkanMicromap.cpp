#include "VulkanMicromap.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <cstring>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		uint64_t BufferAddress(VkBuffer buffer)
		{
			VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
			info.buffer = buffer;
			return vkGetBufferDeviceAddress(GetVulkanDevice(), &info);
		}

		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, VkDeviceSize alignment,
		                  VkBuffer& buffer, VmaAllocation& allocation, const char* debugName)
		{
			VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bufferInfo.size = size;
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

			const VkResult result = alignment > 0
			    ? vmaCreateBufferWithAlignment(GetAllocator(), &bufferInfo, &allocInfo, alignment, &buffer, &allocation, nullptr)
			    : vmaCreateBuffer(GetAllocator(), &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create micromap buffer");
		}
	}

	VulkanMicromap::VulkanMicromap(const uint32_t triangleCount, const uint32_t subdivisionLevel,
	                               const void* statesData, const uint64_t statesSize, const std::string& debugName)
	    : m_TriangleCount(triangleCount), m_SubdivisionLevel(subdivisionLevel)
	{
		const VkDevice device = GetVulkanDevice();
		SS_CORE_ASSERT(triangleCount > 0 && statesData && statesSize > 0, "Empty micromap build");
		SS_CORE_ASSERT(statesSize == static_cast<uint64_t>(triangleCount) * BytesPerTriangle(subdivisionLevel),
		               "Micromap states size mismatch");

		// 1. States buffer (the packed 2-bit opacity values) uploaded to a host-visible device-address buffer.
		//    A build INPUT — freed after the build below.
		// Micromap build inputs (states data + triangle array) must be 256-byte aligned device addresses.
		constexpr VkDeviceSize kMicromapInputAlign = 256;
		VkBuffer statesBuffer = VK_NULL_HANDLE;
		VmaAllocation statesAlloc = nullptr;
		CreateBuffer(statesSize, VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT, true, kMicromapInputAlign,
		             statesBuffer, statesAlloc, "MicromapStates");
		{
			VmaAllocationInfo ai{};
			vmaGetAllocationInfo(GetAllocator(), statesAlloc, &ai);
			std::memcpy(ai.pMappedData, statesData, statesSize);
			vmaFlushAllocation(GetAllocator(), statesAlloc, 0, statesSize);
		}

		// 2. Triangle array: one VkMicromapTriangleEXT per triangle, all at the same uniform subdivision + 4-state
		//    format, each pointing at its slice of the states buffer. Also a build INPUT.
		const uint64_t bytesPerTri = BytesPerTriangle(subdivisionLevel);
		std::vector<VkMicromapTriangleEXT> triangles(triangleCount);
		for (uint32_t i = 0; i < triangleCount; ++i)
		{
			triangles[i].dataOffset = static_cast<uint32_t>(i * bytesPerTri);
			triangles[i].subdivisionLevel = static_cast<uint16_t>(subdivisionLevel);
			triangles[i].format = static_cast<uint16_t>(VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT);
		}
		const VkDeviceSize triArrayBytes = static_cast<VkDeviceSize>(triangleCount) * sizeof(VkMicromapTriangleEXT);
		VkBuffer triArrayBuffer = VK_NULL_HANDLE;
		VmaAllocation triArrayAlloc = nullptr;
		CreateBuffer(triArrayBytes, VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT, true, kMicromapInputAlign,
		             triArrayBuffer, triArrayAlloc, "MicromapTriangleArray");
		{
			VmaAllocationInfo ai{};
			vmaGetAllocationInfo(GetAllocator(), triArrayAlloc, &ai);
			std::memcpy(ai.pMappedData, triangles.data(), triArrayBytes);
			vmaFlushAllocation(GetAllocator(), triArrayAlloc, 0, triArrayBytes);
		}

		// 3. Usage histogram: all triangles share one (subdivisionLevel, 4-state) bucket.
		VkMicromapUsageEXT usage{};
		usage.count = triangleCount;
		usage.subdivisionLevel = subdivisionLevel;
		usage.format = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;

		VkMicromapBuildInfoEXT buildInfo{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
		buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
		buildInfo.flags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
		buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
		buildInfo.usageCountsCount = 1;
		buildInfo.pUsageCounts = &usage;
		buildInfo.data.deviceAddress = BufferAddress(statesBuffer);
		buildInfo.triangleArray.deviceAddress = BufferAddress(triArrayBuffer);
		buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

		// 4. Query sizes, allocate the micromap backing + scratch.
		VkMicromapBuildSizesInfoEXT sizeInfo{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
		vkGetMicromapBuildSizesEXT(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &sizeInfo);

		CreateBuffer(sizeInfo.micromapSize, VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT, false, 0, m_Buffer,
		             m_Allocation, debugName.empty() ? "Micromap" : debugName.c_str());

		VkMicromapCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
		createInfo.buffer = m_Buffer;
		createInfo.offset = 0;
		createInfo.size = sizeInfo.micromapSize;
		createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
		SS_CORE_VERIFY(vkCreateMicromapEXT(device, &createInfo, nullptr, &m_Micromap) == VK_SUCCESS,
		               "Failed to create micromap");

		VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
		    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
		VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		props2.pNext = &asProps;
		vkGetPhysicalDeviceProperties2(GetVulkanPhysicalDevice(), &props2);

		VkBuffer scratchBuffer = VK_NULL_HANDLE;
		VmaAllocation scratchAlloc = nullptr;
		CreateBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
		             asProps.minAccelerationStructureScratchOffsetAlignment, scratchBuffer, scratchAlloc,
		             "MicromapScratch");

		buildInfo.dstMicromap = m_Micromap;
		buildInfo.scratchData.deviceAddress = BufferAddress(scratchBuffer);

		// 5. Build on the graphics queue. ImmediateSubmit fences, so the build is complete (and its transient
		//    inputs free-able) on return; a later BLAS build referencing this micromap is a separate fenced
		//    submit, so queue ordering serializes them without an explicit barrier.
		ImmediateSubmit([&](const VkCommandBuffer cmd) { vkCmdBuildMicromapsEXT(cmd, 1, &buildInfo); });

		vmaDestroyBuffer(GetAllocator(), scratchBuffer, scratchAlloc);
		vmaDestroyBuffer(GetAllocator(), triArrayBuffer, triArrayAlloc);
		vmaDestroyBuffer(GetAllocator(), statesBuffer, statesAlloc);

		if (!debugName.empty())
		{
			SetVulkanObjectName(device, reinterpret_cast<uint64_t>(m_Micromap), VK_OBJECT_TYPE_MICROMAP_EXT,
			                    debugName.c_str());
		}
	}

	VulkanMicromap::~VulkanMicromap()
	{
		if (m_Micromap != VK_NULL_HANDLE)
		{
			vkDestroyMicromapEXT(GetVulkanDevice(), m_Micromap, nullptr);
		}
		if (m_Buffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(GetAllocator(), m_Buffer, m_Allocation);
		}
	}
}
