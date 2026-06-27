#pragma once

#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"

#include <vector>

namespace Snowstorm
{
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
		// Tangent basis for normal mapping. xyz = tangent direction, w = handedness sign for the
		// bitangent (bitangent = cross(normal, tangent.xyz) * tangent.w), the glTF/assimp convention.
		// A plain vec4 keeps the vertex stride 16-byte friendly vs. a separate tangent+bitangent.
		glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f};
	};

	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

		[[nodiscard]] const Ref<Buffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		[[nodiscard]] const Ref<Buffer>& GetIndexBuffer() const { return m_IndexBuffer; }

		[[nodiscard]] uint32_t GetVertexCount() const { return m_VertexCount; }
		[[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }

		[[nodiscard]] const MeshBounds& GetBounds() const { return m_Bounds; }
		void SetBounds(const MeshBounds& b) { m_Bounds = b; }

	private:
		Ref<Buffer> m_VertexBuffer;
		Ref<Buffer> m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		MeshBounds m_Bounds{}; //-- bounds won't be set by default
	};
}
