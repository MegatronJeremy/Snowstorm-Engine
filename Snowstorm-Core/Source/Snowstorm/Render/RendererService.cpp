#include "RendererService.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	namespace
	{
		struct FrameCB
		{
			glm::mat4 ViewProj;
			glm::mat4 InvViewProj; // for reconstructing world-space rays (sky pass); mirrors Engine.hlsli FrameCB
			glm::vec3 CameraPosition;
			float Exposure = 1.0f; // linear pre-tonemap multiplier (mirrors Engine.hlsli FrameCB)

			LightDataBlock Lights;

			// Environment (sky/ambient), shared by Sky.hlsl and DefaultLit's ambient term. Each vec3 is
			// register-packed with the trailing float (SkyIntensity, then padding) — same trick as
			// CameraPosition+Exposure. MUST match the FrameCB tail in Engine.hlsli field-for-field.
			glm::vec3 SkyZenithColor;
			float SkyIntensity = 1.0f;
			glm::vec3 SkyHorizonColor;
			float _EnvPad0 = 0.0f;
			glm::vec3 GroundColor;
			float _EnvPad1 = 0.0f;

			// Directional shadow (sun = DirectionalLights[0]). LightViewProj reprojects world -> light clip
			// for the depth compare; ShadowMapIndex is the bindless index of the shadow depth texture
			// (0 = no shadows, fully lit). The trailing row carries bias + texel size + strength. MUST match
			// the FrameCB tail in Engine.hlsli field-for-field.
			glm::mat4 LightViewProj{1.0f};
			uint32_t ShadowMapIndex = 0;
			float ShadowBias = 0.0015f;
			float ShadowTexelSize = 1.0f / 2048.0f;
			float ShadowStrength = 1.0f;
			uint32_t ShadowSoft = 1;                  // 1 = 3x3 PCF, 0 = hard single tap
			uint32_t SpotShadowAtlasIndex = 0;        // bindless index of the spot shadow atlas (0 = spots unshadowed)
			float _ShadowPad1 = 0.0f;
			float _ShadowPad2 = 0.0f;

			// IBL (Phase 6). Bindless indices of the baked maps: irradiance + prefiltered into the cube
			// array (Cubemaps[]), BRDF LUT into the 2D array (Textures[]). 0 = IBL disabled (use the
			// analytic hemisphere ambient). PrefilteredMipCount drives the roughness->lod mapping. MUST
			// match the FrameCB tail in Engine.hlsli field-for-field.
			uint32_t IrradianceCubeIndex = 0;
			uint32_t PrefilteredCubeIndex = 0;
			uint32_t BRDFLutIndex = 0;
			uint32_t PrefilteredMipCount = 0;
			float IBLIntensity = 1.0f; // separate ambient dial for IBL (irradiance is already convolved)
			float _IBLPad0 = 0.0f;
			float _IBLPad1 = 0.0f;
			float _IBLPad2 = 0.0f;
		};
	}

	void RendererService::NewFrame()
	{
		// Reset the shared instance-buffer cursor once per frame. Each pass (shadow, camera, ...) then
		// appends its instances and records draws with firstInstance at the running cursor, so passes
		// don't overwrite each other's data within the frame.
		m_InstanceWriteCursor = 0;
	}

	void RendererService::BeginScene(const CameraRuntimeComponent& cameraRt,
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
		m_Stats = RenderStats{};
	}

	void RendererService::EndScene()
	{
		Flush();

		m_CommandContext.reset();
		m_FrameIndex = 0;
	}

	void RendererService::DrawMesh(const glm::mat4& transform,
	                                 const Ref<Mesh>& mesh,
	                                 const Ref<MaterialInstance>& materialInstance,
	                                 const uint32_t albedoTextureIndex,
	                                 const glm::vec4& extras0)
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

		InstanceData instance{};
		instance.Model = transform;
		instance.AlbedoTextureIndex = albedoTextureIndex;
		instance.Extras0 = extras0;
		batch->Instances.push_back(instance);
	}

	void RendererService::UploadLights(const LightDataBlock& lightData)
	{
		m_Lights = lightData;
	}

	void RendererService::UploadEnvironment(const EnvironmentDataBlock& environment)
	{
		m_Environment = environment;
	}

	void RendererService::Flush()
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

	Ref<DescriptorSet> RendererService::AcquireFrameSet(const Ref<Pipeline>& pipeline, const uint32_t frameIndex)
	{
		const auto& setLayouts = pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!setLayouts.empty() && setLayouts[0], "Pipeline missing set=0 (Frame) layout");

		// One (set, UBO) per (pipeline, frameIndex). The UBO is kept alive in m_FrameUniformBuffers,
		// keyed by the DescriptorSet* (avoids storing it on DescriptorSet itself).
		auto& perFrameFrameSets = m_FrameSets[pipeline.get()];
		if (perFrameFrameSets.empty())
			perFrameFrameSets.resize(Renderer::GetFramesInFlight());

		if (!perFrameFrameSets[frameIndex])
		{
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Set0_Frame";
			perFrameFrameSets[frameIndex] = DescriptorSet::Create(setLayouts[0], setDesc);
			SS_CORE_ASSERT(perFrameFrameSets[frameIndex], "Failed to create set=0 Frame DescriptorSet");

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

		// Refresh the UBO contents every call (safe + simple; optimize later). Single source of truth for
		// FrameCB assembly, including InvViewProj used by the sky pass.
		FrameCB frame{};
		frame.ViewProj = m_ViewProj;
		frame.InvViewProj = glm::inverse(m_ViewProj);
		frame.CameraPosition = m_CameraPosition;
		frame.Exposure = CVars::Exposure.Get();
		frame.Lights = m_Lights;
		frame.SkyZenithColor = m_Environment.SkyZenithColor;
		frame.SkyHorizonColor = m_Environment.SkyHorizonColor;
		frame.GroundColor = m_Environment.GroundColor;
		frame.SkyIntensity = m_Environment.SkyIntensity;
		frame.LightViewProj = m_LightViewProj;
		frame.ShadowMapIndex = m_ShadowMapIndex;
		frame.ShadowStrength = CVars::ShadowStrength.Get();
		frame.ShadowSoft = CVars::ShadowSoft.Get() ? 1u : 0u;
		frame.ShadowTexelSize = 1.0f / static_cast<float>(m_ShadowResolution != 0 ? m_ShadowResolution : 2048u);
		frame.SpotShadowAtlasIndex = m_SpotShadowAtlasIndex;

		// IBL indices: the bake pass pushes them via SetIBLData only while IBL is enabled (it writes zeros
		// when off), so a non-zero irradiance index means "baked AND on" — turning IBL off leaves the maps
		// baked but the stored indices go to 0 and the shader falls back to the analytic ambient. 0 = off.
		if (m_IrradianceCubeIndex != 0)
		{
			frame.IrradianceCubeIndex = m_IrradianceCubeIndex;
			frame.PrefilteredCubeIndex = m_PrefilteredCubeIndex;
			frame.BRDFLutIndex = m_BRDFLutIndex;
			frame.PrefilteredMipCount = m_PrefilteredMipCount;
			frame.IBLIntensity = CVars::IBLIntensity.Get();
		}

		const Ref<Buffer>& frameUBO = m_FrameUniformBuffers[perFrameFrameSets[frameIndex].get()];
		SS_CORE_ASSERT(frameUBO, "Frame UBO missing for frame descriptor set");
		frameUBO->SetData(&frame, sizeof(FrameCB), 0);

		return perFrameFrameSets[frameIndex];
	}

	void RendererService::SetIBLData(const uint32_t irradianceIndex,
	                                   const uint32_t prefilteredIndex,
	                                   const uint32_t brdfLutIndex,
	                                   const uint32_t prefilteredMipCount)
	{
		m_IrradianceCubeIndex = irradianceIndex;
		m_PrefilteredCubeIndex = prefilteredIndex;
		m_BRDFLutIndex = brdfLutIndex;
		m_PrefilteredMipCount = prefilteredMipCount;
	}

	void RendererService::DrawFullscreenTriangle(const Ref<Pipeline>& pipeline)
	{
		if (!m_CommandContext || !pipeline)
		{
			return;
		}

		m_CommandContext->BindPipeline(pipeline);
		m_CommandContext->BindDescriptorSet(AcquireFrameSet(pipeline, m_FrameIndex), 0);
		m_CommandContext->Draw(3, 1, 0); // fullscreen triangle, no vertex/index buffer
	}

	void RendererService::SetShadowData(const glm::mat4& lightViewProj, const uint32_t shadowMapIndex, const uint32_t shadowResolution)
	{
		m_LightViewProj = lightViewProj;
		m_ShadowMapIndex = shadowMapIndex;
		if (shadowResolution != 0)
		{
			m_ShadowResolution = shadowResolution;
		}
	}

	const Ref<DescriptorSet>& RendererService::AcquireObjectSet(const Ref<Pipeline>& pipeline,
	                                                              const uint32_t frameIndex,
	                                                              const char* debugName)
	{
		// One frame-wide storage buffer holds every instance; the set binds the whole buffer once (fixed
		// capacity → committed once, never re-bound mid-frame). Cached per (pipeline, frame-in-flight).
		EnsureInstanceBuffer(frameIndex, 0);

		auto& perFrameObjectSets = m_ObjectSets[pipeline.get()];
		if (perFrameObjectSets.empty())
			perFrameObjectSets.resize(Renderer::GetFramesInFlight());

		if (!perFrameObjectSets[frameIndex])
		{
			const auto& setLayouts = pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > 2 && setLayouts[2], "Pipeline missing set=2 (Object) layout");

			DescriptorSetDesc setDesc{};
			setDesc.DebugName = debugName;
			perFrameObjectSets[frameIndex] = DescriptorSet::Create(setLayouts[2], setDesc);
			SS_CORE_ASSERT(perFrameObjectSets[frameIndex], "Failed to create set=2 Object DescriptorSet");

			BufferBinding bb{};
			bb.Buffer = m_InstanceBuffers[frameIndex];
			bb.Offset = 0;
			bb.Range = 0; // whole buffer
			perFrameObjectSets[frameIndex]->SetBuffer(0, bb);
			perFrameObjectSets[frameIndex]->Commit();
		}

		return perFrameObjectSets[frameIndex];
	}

	bool RendererService::WriteBatchInstancedDraw(BatchData& batch,
	                                                const Ref<DescriptorSet>& objectSet,
	                                                const char* overflowContext)
	{
		// Write this batch's instances into the frame buffer at the running cursor, then one instanced draw.
		const auto instanceCount = static_cast<uint32_t>(batch.Instances.size());
		const uint32_t firstInstance = m_InstanceWriteCursor;
		if (firstInstance + instanceCount > m_InstanceBufferCapacity)
		{
			SS_CORE_ERROR("Instance buffer overflow{0} ({1}+{2} > {3}); dropping batch.",
			              overflowContext, firstInstance, instanceCount, m_InstanceBufferCapacity);
			return false;
		}

		m_InstanceBuffers[m_FrameIndex]->SetData(batch.Instances.data(),
		                                         instanceCount * sizeof(InstanceData),
		                                         static_cast<size_t>(firstInstance) * sizeof(InstanceData));
		m_InstanceWriteCursor += instanceCount;

		m_CommandContext->BindVertexBuffer(batch.Mesh->GetVertexBuffer(), 0, 0);
		m_CommandContext->BindDescriptorSet(objectSet, 2);
		m_CommandContext->DrawIndexed(batch.Mesh->GetIndexBuffer(),
		                              batch.Mesh->GetIndexCount(),
		                              instanceCount,
		                              0,
		                              0,
		                              firstInstance);
		return true;
	}

	void RendererService::DrawBatchesDepthOnly(const Ref<Pipeline>& depthPipeline, const glm::mat4& lightViewProj)
	{
		if (!m_CommandContext || m_Batches.empty() || !depthPipeline)
		{
			return;
		}

		const auto& setLayouts = depthPipeline->GetSetLayouts();
		SS_CORE_ASSERT(setLayouts.size() > 2 && setLayouts[2], "Depth pipeline missing set 2 (instances)");

		m_CommandContext->BindPipeline(depthPipeline);

		// The light's world->clip matrix travels as a per-draw push constant (see Shadow.vert.hlsl); no
		// set=0/FrameCB binding here, so one caller can re-invoke this with different matrices in one pass.
		m_CommandContext->PushConstants(&lightViewProj, sizeof(glm::mat4), 0);

		const Ref<DescriptorSet>& objectSet = AcquireObjectSet(depthPipeline, m_FrameIndex, "Set2_Instances_Shadow");

		// One instanced depth draw per batch, appending into the shared instance buffer at the running
		// cursor (NewFrame reset it; the camera pass appends after us). Same instance write + draw as the
		// lit FlushBatch, minus materials/bindless. The shadow pass has its own BeginScene accumulation (all
		// casters); the camera pass's BeginScene clears these batches and re-accumulates visible ones — so
		// the batches are NOT cleared here (the camera pass owns clearing).
		for (auto& batch : m_Batches)
		{
			if (batch.Instances.empty() || !batch.Mesh)
				continue;

			WriteBatchInstancedDraw(batch, objectSet, " in shadow pass");
		}
	}

	void RendererService::FlushBatch(BatchData& batch,
	                                   const Ref<CommandContext>& commandContext,
	                                   const uint32_t frameIndex)
	{
		if (batch.Instances.empty())
			return;

		SS_CORE_ASSERT(batch.Mesh && batch.MaterialInstance, "Invalid batch");

		// Stats: one batch == one instanced DrawIndexed covering all its instances.
		const auto batchInstanceCount = static_cast<uint32_t>(batch.Instances.size());
		m_Stats.Batches += 1;
		m_Stats.Instances += batchInstanceCount;
		m_Stats.DrawCalls += 1;
		m_Stats.Triangles += batchInstanceCount * (batch.Mesh->GetIndexCount() / 3u);

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

		// ---- Set 0: Frame ---- (created on demand + uploaded; shared with the sky pass)
		const Ref<DescriptorSet> frameSet = AcquireFrameSet(pipeline, frameIndex);

		// Bind set 0 after the pipeline is bound (pipeline already bound by MaterialInstance::Apply)
		commandContext->BindDescriptorSet(frameSet, 0);

		// ---- Set 2: per-instance Object data (StructuredBuffer) ----
		// One frame-wide storage buffer holds every instance; each batch writes its slice and draws
		// with firstInstance = sliceStart, so SV_InstanceID indexes the right entries.
		const Ref<DescriptorSet>& objectSet = AcquireObjectSet(pipeline, frameIndex, "Set2_Instances");

		// Write this batch's slice + record the instanced draw (shared with the depth-only pass). On
		// overflow the lit path clears the batch so a later pass doesn't retry the dropped instances.
		WriteBatchInstancedDraw(batch, objectSet, "");

		batch.Instances.clear();
	}

	void RendererService::EnsureInstanceBuffer(const uint32_t frameIndex, uint32_t /*additionalNeeded*/)
	{
		if (m_InstanceBuffers.size() <= frameIndex)
		{
			m_InstanceBuffers.resize(frameIndex + 1);
		}

		// Fixed generous capacity, allocated once. Growing mid-frame is unsafe: earlier batches this
		// frame have already recorded draws + descriptor binds against the current buffer, so swapping
		// it would leave them dangling. A per-batch bounds check (in FlushBatch) drops + logs anything
		// past capacity instead. Bump this constant if a scene legitimately needs more.
		constexpr uint32_t kCapacity = 65536; // ~6 MB/frame at sizeof(InstanceData)
		if (!m_InstanceBuffers[frameIndex])
		{
			m_InstanceBufferCapacity = kCapacity;
			m_InstanceBuffers[frameIndex] = Buffer::Create(static_cast<size_t>(kCapacity) * sizeof(InstanceData),
			                                               BufferUsage::Storage, nullptr, true, "InstanceBuffer");
		}
	}
}
