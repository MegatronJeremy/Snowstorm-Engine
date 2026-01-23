#include "RendererSingleton.hpp"

#include "Camera.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
	namespace
	{
		struct FrameCB
		{
			glm::mat4 ViewProj;
			glm::vec3 CameraPosition;
			float _Pad0 = 0.0f;

			LightDataBlock Lights;
		};

		struct ObjectCB
		{
			glm::mat4 Model;
			glm::vec4 Extras0;
		};
	}

	void RendererSingleton::BeginScene(const CameraRuntimeComponent& cameraRt,
	                                   const glm::vec3& cameraWorldPosition,
	                                   const Ref<CommandContext>& commandContext,
	                                   const uint32_t frameIndex)
	{
		SS_CORE_ASSERT(commandContext, "Renderer requires a valid CommandContext");

		m_CommandContext = commandContext;
		m_FrameIndex = frameIndex;

		m_ViewProj = cameraRt.ViewProjection;
		m_CameraPosition = cameraWorldPosition;

		m_Batches.clear();
	}

	void RendererSingleton::EndScene()
	{
		Flush();

		m_CommandContext.reset();
		m_FrameIndex = 0;
	}

	void RendererSingleton::DrawMesh(const glm::mat4& transform,
	                                 const Ref<Mesh>& mesh,
	                                 const Ref<MaterialInstance>& materialInstance)
	{
		SS_CORE_ASSERT(m_CommandContext, "DrawMesh called outside of BeginScene/EndScene");
		SS_CORE_ASSERT(mesh, "Mesh must be valid");
		SS_CORE_ASSERT(materialInstance, "MaterialInstance must be valid");

		BatchData* batch = nullptr;
		for (auto& b : m_Batches)
		{
			if (b.Mesh == mesh && b.MaterialInstance == materialInstance)
			{
				batch = &b;
				break;
			}
		}

		if (!batch)
		{
			BatchData newBatch;
			newBatch.Mesh = mesh;
			newBatch.MaterialInstance = materialInstance;
			m_Batches.push_back(std::move(newBatch));
			batch = &m_Batches.back();
		}

		MeshInstanceData instance{};
		instance.ModelMatrix = transform;
		batch->Instances.push_back(instance);
	}

	void RendererSingleton::UploadLights(const LightDataBlock& lightData)
	{
		m_Lights = lightData;
	}

	void RendererSingleton::Flush()
	{
		if (!m_CommandContext)
		{
			return;
		}

		for (auto& batch : m_Batches)
		{
			FlushBatch(batch, m_CommandContext, m_FrameIndex);
		}
	}

	void RendererSingleton::FlushBatch(BatchData& batch,
	                                   const Ref<CommandContext>& commandContext,
	                                   const uint32_t frameIndex)
	{
		if (batch.Instances.empty())
			return;

		SS_CORE_ASSERT(batch.Mesh && batch.MaterialInstance, "Invalid batch");

		// Bind pipeline + set=1 (Material) for this instance
		batch.MaterialInstance->Apply(*commandContext, frameIndex);

		// Bind engine globals
		// called once per pipeline change or once per frame
		commandContext->BindGlobalResources();

		const Ref<Pipeline>& pipeline = batch.MaterialInstance->GetPipeline();
		SS_CORE_ASSERT(pipeline, "MaterialInstance has no pipeline");

		const auto& setLayouts = pipeline->GetSetLayouts();
		SS_CORE_ASSERT(setLayouts.size() > 2, "Pipeline must provide set layouts 0..2");
		SS_CORE_ASSERT(setLayouts[0] && setLayouts[2], "Pipeline missing set=0 and/or set=2 layouts");

		// ---- Set 0: Frame ----
		auto& perFrameFrameSets = m_FrameSets[pipeline.get()];
		if (perFrameFrameSets.empty())
			perFrameFrameSets.resize(Renderer::GetFramesInFlight());

		// One UBO per (pipeline, frameIndex). Keep it alive by storing it in a static map keyed by DescriptorSet*.
		// This avoids adding more members to DescriptorSet for now.
		if (!perFrameFrameSets[frameIndex])
		{
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Set0_Frame";
			perFrameFrameSets[frameIndex] = DescriptorSet::Create(setLayouts[0], setDesc);
			SS_CORE_ASSERT(perFrameFrameSets[frameIndex], "Failed to create set=0 Frame DescriptorSet");

			// Create the frame CB backing this descriptor set
			Ref<Buffer> frameCB = Buffer::Create(sizeof(FrameCB), BufferUsage::Uniform, nullptr, true, "FrameCB");
			SS_CORE_ASSERT(frameCB, "Failed to create FrameCB uniform buffer");

			m_FrameUniformBuffers[perFrameFrameSets[frameIndex].get()] = frameCB;

			BufferBinding bb{};
			bb.Buffer = frameCB;
			bb.Offset = 0;
			bb.Range = sizeof(FrameCB);
			perFrameFrameSets[frameIndex]->SetBuffer(0, bb);
			perFrameFrameSets[frameIndex]->Commit();
		}

		// Update frame UBO contents every flush (safe + simple; optimize later)
		{
			FrameCB frame{};
			frame.ViewProj = m_ViewProj;
			frame.CameraPosition = m_CameraPosition;
			frame.Lights = m_Lights;

			Ref<Buffer>& frameUBO = m_FrameUniformBuffers[perFrameFrameSets[frameIndex].get()];
			SS_CORE_ASSERT(frameUBO, "Frame UBO missing for frame descriptor set");
			frameUBO->SetData(&frame, sizeof(FrameCB), 0);
		}

		// Bind set 0 after the pipeline is bound (pipeline already bound by MaterialInstance::Apply)
		commandContext->BindDescriptorSet(perFrameFrameSets[frameIndex], 0);

		// ---- Set 2: Object (dynamic) ----
		auto& perFrameObjectSets = m_ObjectSets[pipeline.get()];
		if (perFrameObjectSets.empty())
			perFrameObjectSets.resize(Renderer::GetFramesInFlight());

		if (!perFrameObjectSets[frameIndex])
		{
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Set2_Object";
			perFrameObjectSets[frameIndex] = DescriptorSet::Create(setLayouts[2], setDesc);
			SS_CORE_ASSERT(perFrameObjectSets[frameIndex], "Failed to create set=2 Object DescriptorSet");

			BufferBinding bb{};
			bb.Buffer = Renderer::GetFrameUniformRing().GetBuffer();
			bb.Offset = 0;
			bb.Range = sizeof(ObjectCB);
			perFrameObjectSets[frameIndex]->SetBuffer(0, bb);
			perFrameObjectSets[frameIndex]->Commit();
		}

		const Ref<DescriptorSet>& objectSet = perFrameObjectSets[frameIndex];

		// Bind mesh buffers
		commandContext->BindVertexBuffer(batch.Mesh->GetVertexBuffer(), 0, 0);

		auto& ring = Renderer::GetFrameUniformRing();
		const uint32_t alignment = Renderer::GetMinUniformBufferOffsetAlignment();

		for (const auto& instance : batch.Instances)
		{
			ObjectCB obj{};
			obj.Model = instance.ModelMatrix;
			obj.Extras0 = batch.MaterialInstance->GetObjectExtras0();

			const auto alloc = ring.AllocAndWrite(&obj, sizeof(ObjectCB), alignment);
			const uint32_t dynamicOffset = alloc.Offset;

			commandContext->BindDescriptorSet(objectSet, 2, &dynamicOffset, 1);

			// TODO only one instance?
			commandContext->DrawIndexed(batch.Mesh->GetIndexBuffer(),
			                            batch.Mesh->GetIndexCount(),
			                            1,
			                            0,
			                            0);
		}

		batch.Instances.clear();
	}
}
