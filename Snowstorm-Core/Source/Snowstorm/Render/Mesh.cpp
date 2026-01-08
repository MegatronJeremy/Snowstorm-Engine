#include "Mesh.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices):
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

		m_IndexBuffer = Buffer::Create(
			sizeof(uint32_t) * indices.size(),
			BufferUsage::Index,
			indices.data(),
			false,
			"Mesh Index Buffer");

		SS_CORE_ASSERT(m_VertexBuffer, "Failed to create mesh vertex buffer");
		SS_CORE_ASSERT(m_IndexBuffer, "Failed to create mesh index buffer");
	}
}
