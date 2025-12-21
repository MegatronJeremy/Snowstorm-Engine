#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"

namespace Snowstorm
{
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
	};

	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

		[[nodiscard]] const Ref<Buffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		[[nodiscard]] const Ref<Buffer>& GetIndexBuffer() const { return m_IndexBuffer; }

		[[nodiscard]] uint32_t GetVertexCount() const { return m_VertexCount; }
		[[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }

	private:
		Ref<Buffer> m_VertexBuffer;
		Ref<Buffer> m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;
	};
}
