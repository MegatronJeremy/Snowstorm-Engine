#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"

#include <cstdint>
#include <string>

namespace Snowstorm
{
	// Bottom-level acceleration structure (#118): the ray-traced triangle geometry of a single mesh, built
	// once and reused across frames and TLAS instances. Backend-agnostic handle so it can be cached on the
	// (platform-independent) Mesh; the Vulkan impl wraps a VkAccelerationStructureKHR + its backing buffer.
	// Create only when the device supports RT (Renderer::IsRayTracingSupported()).
	class BLAS
	{
	public:
		virtual ~BLAS() = default;

		// GPU device address of the built AS, used as an instance's accelerationStructureReference in a TLAS.
		[[nodiscard]] virtual uint64_t GetDeviceAddress() const = 0;

		// Build a triangle BLAS from a mesh's vertex/index buffers (both must carry the AS-build-input usage;
		// see VulkanBuffer). positionOffset + vertexStride locate the R32G32B32 position inside each vertex.
		// Synchronous — builds on ImmediateSubmit (graphics queue) and returns once complete.
		static Ref<BLAS> Create(const Ref<Buffer>& vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
		                        uint32_t positionOffset, const Ref<Buffer>& indexBuffer, uint32_t indexCount,
		                        const std::string& debugName = "");
	};
}
