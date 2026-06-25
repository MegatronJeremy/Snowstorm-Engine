#pragma once

#include "CommandContext.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
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

	// Per-scene-pass GPU submission stats, for the editor's perf overlay. Reset each BeginScene and
	// filled during Flush, so it reflects the most recent scene pass. DrawCalls == Instances today
	// (one DrawIndexed per object) and Batches tracks how well (mesh, material) batching collapses
	// objects — both are the headline numbers for diagnosing draw-submission cost.
	struct RenderStats
	{
		uint32_t Batches = 0;   // unique (mesh, materialInstance) groups
		uint32_t Instances = 0; // total renderables submitted
		uint32_t DrawCalls = 0; // vkCmdDrawIndexed invocations
		uint32_t Triangles = 0; // total triangles submitted
	};

	class RendererSingleton final : public Singleton
	{
	public:
		void BeginScene(const CameraRuntimeComponent& cameraRt,
		                const glm::vec3& cameraWorldPosition,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex);

		void EndScene();

		void DrawMesh(const glm::mat4& transform,
		              const Ref<Mesh>& mesh,
		              const Ref<MaterialInstance>& materialInstance);

		void UploadLights(const LightDataBlock& lightData);

		void Flush();

		// Stats from the most recently submitted scene pass (reset each BeginScene).
		[[nodiscard]] const RenderStats& GetStats() const { return m_Stats; }

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

		RenderStats m_Stats{};
	};
}
