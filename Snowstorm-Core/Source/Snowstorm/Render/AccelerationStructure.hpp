#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Render/Buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Opacity micromap (VK_EXT_opacity_micromap, RDNA4 / Ada+): a per-triangle table of per-microtriangle
	// opacity states (4-state, 2 bits each) attached to a BLAS so the hardware resolves cutout coverage during
	// traversal and invokes the any-hit alpha test only on UNKNOWN (edge) microtriangles. Backend-agnostic
	// handle mirroring BLAS/TLAS; the Vulkan impl wraps a VkMicromapEXT. Create only when the device supports it
	// (Renderer::SupportsOpacityMicromap()).
	class Micromap
	{
	public:
		virtual ~Micromap() = default;

		[[nodiscard]] virtual uint32_t GetTriangleCount() const = 0;
		[[nodiscard]] virtual uint32_t GetSubdivisionLevel() const = 0;

		// Packed 4-state (2 bits/microtriangle) bytes for one triangle at a uniform subdivision level: 4^level
		// microtriangles / 4 per byte, at least 1. The states buffer is triangleCount * this.
		[[nodiscard]] static uint64_t BytesPerTriangle(const uint32_t subdivisionLevel)
		{
			const uint64_t microTris = 1ull << (2u * subdivisionLevel); // 4^level
			return microTris < 4 ? 1 : microTris / 4;
		}

		// Build a 4-state opacity micromap over triangleCount triangles at a uniform subdivision level, from
		// packed 2-bit states (statesData, statesSize = triangleCount * BytesPerTriangle(level)). Synchronous.
		static Ref<Micromap> Create(uint32_t triangleCount, uint32_t subdivisionLevel, const void* statesData,
		                            uint64_t statesSize, const std::string& debugName = "");
	};

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
		// Synchronous — builds on ImmediateSubmit (graphics queue) and returns once complete. When `micromap`
		// is non-null the geometry is built non-opaque with the micromap chained in, so cutout coverage is
		// resolved per-microtriangle during traversal (the alpha any-hit runs only on UNKNOWN microtriangles).
		static Ref<BLAS> Create(const Ref<Buffer>& vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
		                        uint32_t positionOffset, const Ref<Buffer>& indexBuffer, uint32_t indexCount,
		                        const std::string& debugName = "", const Ref<Micromap>& micromap = nullptr);
	};

	// One renderable in a TLAS: a mesh's BLAS placed by a world transform. The build reads BlasAddress
	// (from BLAS::GetDeviceAddress()) and the upper 3x4 of Transform.
	struct TLASInstance
	{
		glm::mat4 Transform{1.0f};
		uint64_t BlasAddress = 0;
		// Alpha-cutout (glTF MASK): when true the TLAS flags this instance FORCE_NON_OPAQUE so RayQuery surfaces
		// its triangles as candidates and the any-hit alpha test (RTCommitCandidate) runs. Without it the global
		// OPAQUE geometry bit auto-commits every hit and cutout foliage renders solid across ALL RT passes (#151).
		bool ForceNonOpaque = false;
	};

	// Top-level acceleration structure (#118): the scene's set of instanced BLASes that ray-query shaders
	// trace against. Rebuilt/refit per frame as the scene changes (TlasBuildSystem). Backend-agnostic handle;
	// the Vulkan impl owns a VkAccelerationStructureKHR + instance/scratch buffers and exposes its handle to
	// the bindless descriptor. Create only when the device supports RT.
	class TLAS
	{
	public:
		virtual ~TLAS() = default;

		// (Re)build the TLAS from the given instances. Synchronous — builds on ImmediateSubmit (graphics
		// queue). Empty instance list => an empty (but valid) TLAS. A full rebuild each call: the scene's
		// instance count is small and TlasBuildSystem only calls this when the scene actually changed, so the
		// cost is negligible; incremental refit (UPDATE mode) is a deferred optimization, not needed here.
		virtual void Build(const std::vector<TLASInstance>& instances) = 0;

		[[nodiscard]] virtual uint32_t GetInstanceCount() const = 0;

		static Ref<TLAS> Create(const std::string& debugName = "");
	};
}
