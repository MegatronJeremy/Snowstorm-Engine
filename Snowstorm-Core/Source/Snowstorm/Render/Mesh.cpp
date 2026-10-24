#include "Mesh.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
	           const IndexType indexType) : m_IndexType(indexType),
	                                        m_VertexCount(static_cast<uint32_t>(vertices.size())),
	                                        m_IndexCount(static_cast<uint32_t>(indices.size()))
	{
		SS_CORE_ASSERT(m_VertexCount > 0, "Mesh must have vertices");
		SS_CORE_ASSERT(m_IndexCount > 0, "Mesh must have indices");

		m_VertexBuffer = Buffer::Create(
		    sizeof(Vertex) * vertices.size(),
		    BufferUsage::Vertex,
		    vertices.data(),
		    false,
		    "Mesh Vertex Buffer");

		const std::vector<uint8_t> packed = PackIndices(indices, indexType);
		SS_CORE_ASSERT(indexType == IndexType::UInt32 || m_VertexCount <= kMaxUInt16Index + 1u,
		               "UInt16 index type needs <= 65535 vertices");
		m_IndexBuffer = Buffer::Create(
		    packed.size(),
		    BufferUsage::Index,
		    packed.data(),
		    false,
		    "Mesh Index Buffer");

		if (indexType == IndexType::UInt32 && Renderer::IsRayTracingSupported())
		{
			m_CpuPositions.reserve(vertices.size());
			for (const Vertex& v : vertices)
			{
				m_CpuPositions.push_back(v.Position);
			}
			m_CpuIndices = indices;
		}

		SS_CORE_ASSERT(m_VertexBuffer, "Failed to create mesh vertex buffer");
		SS_CORE_ASSERT(m_IndexBuffer, "Failed to create mesh index buffer");
	}

	const Ref<BLAS>& Mesh::GetOrBuildBLAS()
	{
		if (!m_BLAS && m_IndexType == IndexType::UInt32)
		{
			// Position is the first Vertex field (offset 0), stride is the whole vertex. The vertex/index
			// buffers carry the AS-build-input usage (added in VulkanBuffer when RT is on), so the build reads
			// them by device address.
			m_BLAS = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                      m_IndexBuffer, m_IndexCount, "Mesh BLAS");
		}
		return m_BLAS;
	}

	const Ref<BLAS>& Mesh::GetOrBuildOmmBlas(const uint32_t subdivisionLevel, const uint32_t albedoTextureIndex,
	                                         const float alphaCutoff, const float baseColorAlpha)
	{
		if (!m_OmmBlas && m_IndexType == IndexType::UInt32)
		{
			const uint32_t triangleCount = m_IndexCount / 3;
			// GPU-bake the micromap states from the material's albedo alpha, then attach to a non-opaque BLAS.
			// Null when the bake pipeline is still compiling: leave m_OmmBlas null (returned below) so the gather
			// falls back to the any-hit path this frame and retries next.
			const Ref<Micromap> micromap =
			    Micromap::CreateBaked(m_VertexBuffer->GetGPUAddress(), m_IndexBuffer->GetGPUAddress(), triangleCount,
			                          subdivisionLevel, albedoTextureIndex, alphaCutoff, baseColorAlpha, "Mesh OMM");
			if (!micromap)
			{
				return m_OmmBlas; // still null; retried next call
			}
			m_OmmBlas = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                         m_IndexBuffer, m_IndexCount, "Mesh OMM BLAS", micromap);
		}
		return m_OmmBlas;
	}
}
