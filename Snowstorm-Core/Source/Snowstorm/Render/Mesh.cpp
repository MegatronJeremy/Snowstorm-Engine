#include "Mesh.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) : m_VertexCount(static_cast<uint32_t>(vertices.size())),
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

		m_IndexBuffer = Buffer::Create(
		    sizeof(uint32_t) * indices.size(),
		    BufferUsage::Index,
		    indices.data(),
		    false,
		    "Mesh Index Buffer");

		SS_CORE_ASSERT(m_VertexBuffer, "Failed to create mesh vertex buffer");
		SS_CORE_ASSERT(m_IndexBuffer, "Failed to create mesh index buffer");
	}

	const Ref<BLAS>& Mesh::GetOrBuildBLAS()
	{
		if (!m_BLAS)
		{
			// Position is the first Vertex field (offset 0), stride is the whole vertex. The vertex/index
			// buffers carry the AS-build-input usage (added in VulkanBuffer when RT is on), so the build reads
			// them by device address.
			m_BLAS = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                      m_IndexBuffer, m_IndexCount, "Mesh BLAS");
		}
		return m_BLAS;
	}

	const Ref<BLAS>& Mesh::GetOrBuildOmmBlas(const uint32_t subdivisionLevel)
	{
		if (!m_OmmBlas)
		{
			const uint32_t triangleCount = m_IndexCount / 3;
			// B1: all-UNKNOWN_OPAQUE — each microtriangle's 2 bits = 0b11, so every state byte is 0xFF. The
			// any-hit still runs on every microtriangle, so the image is identical; this only proves the OMM
			// build + BLAS-attach path. B2 replaces this fill with a compute bake of real OPAQUE/TRANSPARENT/
			// UNKNOWN states, at which point interior microtriangles skip the any-hit.
			const std::vector<uint8_t> states(
			    static_cast<size_t>(triangleCount) * Micromap::BytesPerTriangle(subdivisionLevel), 0xFF);
			const Ref<Micromap> micromap =
			    Micromap::Create(triangleCount, subdivisionLevel, states.data(), states.size(), "Mesh OMM");
			m_OmmBlas = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                         m_IndexBuffer, m_IndexCount, "Mesh OMM BLAS", micromap);
		}
		return m_OmmBlas;
	}
}
