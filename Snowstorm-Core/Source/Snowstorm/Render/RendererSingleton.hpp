#pragma once

#include "Camera.hpp"
#include "CommandContext.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Pipeline.hpp"

#include <unordered_map>

namespace Snowstorm
{
	struct MeshInstanceData
	{
		glm::mat4 ModelMatrix;
	};

	struct BatchData
	{
		Ref<Mesh> Mesh;
		Ref<MaterialInstance> MaterialInstance;
		std::vector<MeshInstanceData> Instances;
	};

	class RendererSingleton final : public Singleton
	{
	public:
		RendererSingleton() = default;

		void BeginScene(const Camera& camera,
		                const glm::mat4& cameraTransform,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex);

		void EndScene();

		void DrawMesh(const glm::mat4& transform,
		              const Ref<Mesh>& mesh,
		              const Ref<MaterialInstance>& materialInstance);

		void UploadLights(const LightDataBlock& lightData);

		void Flush();

	private:
		void FlushBatch(BatchData& batch,
		                       const Ref<CommandContext>& commandContext,
		                       uint32_t frameIndex);

	private:
		Ref<CommandContext> m_CommandContext;
		uint32_t m_FrameIndex = 0;

		glm::mat4 m_ViewProj{1.0f};
		glm::vec3 m_CameraPosition{0.0f};
		LightDataBlock m_Lights{};

		std::vector<BatchData> m_Batches;

		// Cached per-pipeline sets, per frame-in-flight
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_FrameSets;
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_ObjectSets;

		std::unordered_map<const DescriptorSet*, Ref<Buffer>> m_FrameUniformBuffers;
	};
}
