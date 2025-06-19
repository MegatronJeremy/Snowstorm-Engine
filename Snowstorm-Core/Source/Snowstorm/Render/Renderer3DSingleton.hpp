#pragma once

#include "Camera.hpp"
#include "Material.hpp"
#include "Mesh.hpp"
#include "UniformBuffer.hpp"
#include "VertexArray.hpp"

#include "Snowstorm/Lighting/LightingUniforms.hpp"

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

		void UploadLights(const LightDataBlock& lightData) const;

		void SetSkybox(const Ref<Material>& skyboxMaterial, const Ref<TextureCube>& texture);

		void Flush();

	private:
		static void FlushBatch(BatchData& batch);

		void DrawSkybox(const glm::mat4& view, const glm::mat4& proj) const;

		Ref<UniformBuffer> m_CameraUBO;
		Ref<UniformBuffer> m_LightUBO;
		std::vector<BatchData> m_Batches;

		Ref<Material> m_SkyboxMaterial;
		Ref<Mesh> m_SkyboxMesh;
		Ref<VertexArray> m_SkyboxVAO;
	};
}
