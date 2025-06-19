#include "Renderer3DSingleton.hpp"

#include "RenderCommand.hpp"

namespace
{
	// TODO move this somewhere else, like static helpers for creating meshes?
	float skyboxVertices[] = {
		-1.0f,  1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f, -1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,

		-1.0f, -1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f,
		-1.0f, -1.0f,  1.0f,

		-1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f, -1.0f,
		 1.0f,  1.0f,  1.0f,
		 1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f,  1.0f,
		-1.0f,  1.0f, -1.0f,

		-1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f, -1.0f,
		 1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f,  1.0f,
		 1.0f, -1.0f,  1.0f
	};
}

namespace Snowstorm
{
	Renderer3DSingleton::Renderer3DSingleton()
	{
		m_CameraUBO = UniformBuffer::Create(sizeof(glm::mat4), 0);
		m_LightUBO = UniformBuffer::Create(sizeof(LightDataBlock), 1);

		m_SkyboxVAO = VertexArray::Create();

		Ref<VertexBuffer> vbo = VertexBuffer::Create(skyboxVertices, sizeof(skyboxVertices));
		vbo->SetLayout({ { ShaderDataType::Float3, "a_Position" } });

		m_SkyboxVAO->AddVertexBuffer(vbo);
	}

	void Renderer3DSingleton::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		const glm::mat4 view = glm::inverse(transform);
		const glm::mat4 proj = camera.GetProjection();
		const glm::mat4 viewProj = proj * view;
		m_CameraUBO->SetData(&viewProj, sizeof(glm::mat4));

		DrawSkybox(view, proj);

		m_Batches.clear();
	}

	void Renderer3DSingleton::EndScene()
	{
		Flush();
	}

	void Renderer3DSingleton::DrawMesh(const glm::mat4& transform, const Ref<Mesh>& mesh, const Ref<Material>& material)
	{
		BatchData* batch = nullptr;
		for (auto& b : m_Batches)
		{
			if (b.Mesh == mesh && b.Material == material)
			{
				batch = &b;
				break;
			}
		}

		if (!batch)
		{
			// Create new batch
			BatchData newBatch;
			newBatch.Mesh = mesh;
			newBatch.Material = material;
			newBatch.VAO = VertexArray::Create();
			newBatch.VAO->Bind();

			newBatch.VBO = VertexBuffer::Create(mesh->GetVertices().data(), mesh->GetVertexCount() * sizeof(Vertex));
			newBatch.IBO = IndexBuffer::Create(mesh->GetIndices().data(), mesh->GetIndexCount());

			// **Get vertex attributes dynamically from the material**
			const auto vertexLayout = material->GetVertexLayout();
			const auto instanceLayout = material->GetInstanceLayout();

			newBatch.VBO->SetLayout(vertexLayout);

			newBatch.InstanceBuffer = VertexBuffer::Create(1000 * sizeof(MeshInstanceData));
			newBatch.InstanceBuffer->SetLayout(instanceLayout);

			newBatch.VAO->AddVertexBuffer(newBatch.VBO);
			newBatch.VAO->AddVertexBuffer(newBatch.InstanceBuffer);
			newBatch.VAO->SetIndexBuffer(newBatch.IBO);

			m_Batches.push_back(std::move(newBatch));
			batch = &m_Batches.back();
		}

		// Add instance data
		MeshInstanceData instance;
		instance.ModelMatrix = transform;

		batch->Instances.push_back(instance);

		if (batch->Instances.size() >= 1000)
		{
			FlushBatch(*batch);
			batch->Instances.clear();
		}
	}

	void Renderer3DSingleton::UploadLights(const LightDataBlock& lightData) const
	{
		m_LightUBO->SetData(&lightData, sizeof(LightDataBlock));
	}

	void Renderer3DSingleton::SetSkybox(const Ref<Material>& skyboxMaterial, const Ref<TextureCube>& texture)
	{
		m_SkyboxMaterial = skyboxMaterial;
		m_SkyboxMaterial->SetUniform("u_Skybox", 0);
		m_SkyboxMaterial->SetTexture(0, texture);
	}

	void Renderer3DSingleton::Flush()
	{
		for (auto& batch : m_Batches)
		{
			FlushBatch(batch);
		}
	}

	void Renderer3DSingleton::FlushBatch(BatchData& batch)
	{
		if (batch.Instances.empty()) return;

		batch.InstanceBuffer->SetData(batch.Instances.data(), batch.Instances.size() * sizeof(MeshInstanceData));

		batch.VAO->Bind();
		batch.Material->Bind();

		RenderCommand::DrawIndexedInstanced(batch.VAO, batch.Mesh->GetIndexCount(), static_cast<uint32_t>(batch.Instances.size()));

		batch.Instances.clear();
	}

	void Renderer3DSingleton::DrawSkybox(const glm::mat4& view, const glm::mat4& proj) const
	{
		if (!m_SkyboxMaterial || !m_SkyboxVAO)
			return;

		m_SkyboxMaterial->SetUniform("u_View", glm::mat4(glm::mat3(view))); // strip translation
		m_SkyboxMaterial->SetUniform("u_Projection", proj);

		m_SkyboxMaterial->Bind();
		m_SkyboxVAO->Bind();

		RenderCommand::SetDepthMask(false); // prevent skybox from writing depth
		RenderCommand::DrawArrays(m_SkyboxVAO, 36);
		RenderCommand::SetDepthMask(true);
	}
}
