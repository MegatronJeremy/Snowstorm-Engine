#pragma once

#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/AccelerationStructure.hpp"
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

		// The mesh's ray-tracing BLAS, built lazily on first call and cached (#118). Null when the device has
		// no RT support. Built from this mesh's own vertex/index buffers (Position at offset 0, stride
		// sizeof(Vertex)); a TLAS instance references its device address. Callers gate on RT support.
		[[nodiscard]] const Ref<BLAS>& GetOrBuildBLAS();

	private:
		Ref<Buffer> m_VertexBuffer;
		Ref<Buffer> m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		MeshBounds m_Bounds{}; //-- bounds won't be set by default

		Ref<BLAS> m_BLAS; // lazily built on first GetOrBuildBLAS(); null until then / when RT unsupported
	};
}
