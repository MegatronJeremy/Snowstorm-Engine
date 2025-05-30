#pragma once

#include "Camera.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "UniformBuffer.hpp"
#include "VertexArray.hpp"

namespace Snowstorm
{
	struct MeshInstanceData
	{
		glm::mat4 ModelMatrix;
	};

	struct BatchData
	{
		Ref<Mesh> Mesh;
		Ref<Material> Material;
		Ref<VertexArray> VAO;
		Ref<VertexBuffer> VBO;
		Ref<VertexBuffer> InstanceBuffer;
		Ref<IndexBuffer> IBO;
		std::vector<MeshInstanceData> Instances;
	};

	class Renderer3DSingleton final : public Singleton
	{
	public:
		Renderer3DSingleton();
		void BeginScene(const Camera& camera, const glm::mat4& transform);
		void EndScene();
		void DrawMesh(const glm::mat4& transform, const Ref<Mesh>& mesh, const Ref<Material>& material);
		void Flush();

	private:
		static void FlushBatch(BatchData& batch);

		Ref<UniformBuffer> m_CameraUBO;
		std::vector<BatchData> m_Batches;
	};
}
