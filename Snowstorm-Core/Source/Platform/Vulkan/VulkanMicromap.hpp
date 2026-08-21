#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/AccelerationStructure.hpp"

#include <cstdint>
#include <string>

namespace Snowstorm
{
	// Opacity micromap (VK_EXT_opacity_micromap): a per-triangle table of per-microtriangle opacity states
	// (4-state: OPAQUE / TRANSPARENT / UNKNOWN_OPAQUE / UNKNOWN_TRANSPARENT, 2 bits each) that a BLAS carries so
	// the hardware resolves cutout coverage during traversal and only invokes the any-hit alpha test on UNKNOWN
	// (edge) microtriangles. Built from a device buffer of packed 2-bit states — B1 fills it trivially (all
	// UNKNOWN) to prove the build/attach path; B2 fills it via a compute bake that samples the albedo alpha.
	//
	// Every triangle uses the SAME uniform subdivision level, so microtriangle count per triangle is 4^level and
	// each triangle's states occupy 4^level * 2 bits = 4^(level-1) bytes at dataOffset = triangleIndex * that.
	//
	// Only construct when VulkanContext::SupportsOpacityMicromap() — volk loads the vkCmd*Micromap* entry points
	// only where the extension is enabled (the OMM-capable GPU), so calling them otherwise dereferences null.
	class VulkanMicromap final : public Micromap
	{
	public:
		// Build synchronously (ImmediateSubmit) from CPU-side packed 4-state states (statesData, statesSize
		// bytes = triangleCount * BytesPerTriangle(level)). Uploads to a device buffer, builds the triangle
		// array + micromap, then frees the transient inputs. The states data ordering within a triangle follows
		// the micromap's microtriangle addressing; irrelevant for a uniform (all-same-state) fill.
		VulkanMicromap(uint32_t triangleCount, uint32_t subdivisionLevel, const void* statesData,
		               uint64_t statesSize, const std::string& debugName);
		~VulkanMicromap();

		VulkanMicromap(const VulkanMicromap&) = delete;
		VulkanMicromap& operator=(const VulkanMicromap&) = delete;

		[[nodiscard]] VkMicromapEXT GetHandle() const { return m_Micromap; }
		[[nodiscard]] uint32_t GetTriangleCount() const override { return m_TriangleCount; }
		[[nodiscard]] uint32_t GetSubdivisionLevel() const override { return m_SubdivisionLevel; }

	private:
		VkMicromapEXT m_Micromap = VK_NULL_HANDLE;
		VkBuffer m_Buffer = VK_NULL_HANDLE; // backing store for the built micromap
		VmaAllocation m_Allocation = nullptr;
		uint32_t m_TriangleCount = 0;
		uint32_t m_SubdivisionLevel = 0;
	};
}
