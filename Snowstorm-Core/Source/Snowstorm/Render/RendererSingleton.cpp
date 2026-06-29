#include "RendererSingleton.hpp"

#include <algorithm>
#include <cstddef>

#include "Camera.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
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
			uint32_t ShadowSoft = 1; // 1 = 3x3 PCF, 0 = hard single tap
			float _ShadowPad0 = 0.0f;
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

	void RendererSingleton::NewFrame()
	{
		// Reset the shared instance-buffer cursor once per frame. Each pass (shadow, camera, ...) then
		// appends its instances and records draws with firstInstance at the running cursor, so passes
		// don't overwrite each other's data within the frame.
		m_InstanceWriteCursor = 0;
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
		m_Stats = RenderStats{};
	}

	void RendererSingleton::EndScene()
	{
		Flush();

		m_CommandContext.reset();
		m_FrameIndex = 0;
	}

	void RendererSingleton::DrawMesh(const glm::mat4& transform,
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

	void RendererSingleton::UploadLights(const LightDataBlock& lightData)
	{
		m_Lights = lightData;
	}

	void RendererSingleton::UploadEnvironment(const EnvironmentDataBlock& environment)
	{
		m_Environment = environment;
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

	Ref<DescriptorSet> RendererSingleton::AcquireFrameSet(const Ref<Pipeline>& pipeline, const uint32_t frameIndex)
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
		frame.ShadowTexelSize = 1.0f / static_cast<float>(m_ShadowTarget ? m_ShadowTarget->GetWidth() : 2048u);

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

	void RendererSingleton::SetIBLData(const uint32_t irradianceIndex,
	                                   const uint32_t prefilteredIndex,
	                                   const uint32_t brdfLutIndex,
	                                   const uint32_t prefilteredMipCount)
	{
		m_IrradianceCubeIndex = irradianceIndex;
		m_PrefilteredCubeIndex = prefilteredIndex;
		m_BRDFLutIndex = brdfLutIndex;
		m_PrefilteredMipCount = prefilteredMipCount;
	}

	void RendererSingleton::DrawSky(const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		if (!m_CommandContext)
		{
			return;
		}

		// Sky is opt-in: only the procedural-sky background mode draws. No environment / SolidColor mode
		// leaves the render target's clear color showing (see EnvironmentDataBlock::DrawProceduralSky).
		if (!m_Environment.DrawProceduralSky)
		{
			return;
		}

		// (Re)build the sky pipeline when first used or when the target formats change. Empty vertex
		// layout (the VS generates a fullscreen triangle from SV_VertexID); depth test LessOrEqual with
		// NO depth write so the sky sits at the far plane behind already-drawn geometry.
		if (!m_SkyPipeline || m_SkyColorFormat != colorFormat || m_SkyDepthFormat != depthFormat)
		{
			Ref<Shader> shader = Shader::Create("assets/shaders/Sky.hlsl");
			SS_CORE_ASSERT(shader, "Failed to load Sky shader");

			PipelineDesc p{};
			p.Type = PipelineType::Graphics;
			p.Shader = shader;
			p.ColorFormats = {colorFormat};
			p.DepthFormat = depthFormat;
			p.Raster.Cull = CullMode::None; // fullscreen triangle: don't cull by winding
			p.DepthStencil.EnableDepthTest = true;
			p.DepthStencil.EnableDepthWrite = false;
			p.DepthStencil.DepthCompare = CompareOp::LessOrEqual;
			p.DebugName = "SkyPipeline";

			m_SkyPipeline = Pipeline::Create(p);
			SS_CORE_ASSERT(m_SkyPipeline, "Failed to create Sky pipeline");
			m_SkyColorFormat = colorFormat;
			m_SkyDepthFormat = depthFormat;
		}

		m_CommandContext->BindPipeline(m_SkyPipeline);
		m_CommandContext->BindDescriptorSet(AcquireFrameSet(m_SkyPipeline, m_FrameIndex), 0);
		m_CommandContext->Draw(3, 1, 0); // fullscreen triangle, no vertex/index buffer
	}

	void RendererSingleton::SetShadowData(const glm::mat4& lightViewProj, const uint32_t shadowMapIndex)
	{
		m_LightViewProj = lightViewProj;
		m_ShadowMapIndex = shadowMapIndex;
	}

	const Ref<RenderTarget>& RendererSingleton::GetOrCreateShadowTarget()
	{
		// Resolution is a runtime quality setting (render.shadow.resolution). Clamp to a sane range and
		// rebuild the target when it changes. GPUs are idle between frames here (single in-flight wait in
		// BeginFrame), so recreating the image is safe at this call site (start of frame, before any pass).
		const int requested = std::clamp(CVars::ShadowResolution.Get(), 256, 8192);
		const auto size = static_cast<uint32_t>(requested);

		if (!m_ShadowTarget || m_ShadowTarget->GetWidth() != size)
		{
			m_ShadowTarget = CreateShadowDepthTarget(size, "Sun");
			SS_CORE_ASSERT(m_ShadowTarget, "Failed to create shadow depth target");
		}
		return m_ShadowTarget;
	}

	void RendererSingleton::DrawShadowDepth(const PixelFormat depthFormat)
	{
		if (!m_CommandContext || m_Batches.empty())
		{
			return;
		}

		// Lazy depth-only pipeline: mesh vertex layout (position is all the shadow VS reads), depth
		// test+write ON, no color target. Rebuilt only if the depth format changes.
		if (!m_ShadowPipeline || m_ShadowDepthFormat != depthFormat)
		{
			Ref<Shader> shader = Shader::Create("assets/shaders/Shadow.hlsl");
			SS_CORE_ASSERT(shader, "Failed to load Shadow shader");

			// Same vertex layout as the lit mesh pipeline (set in AssetManagerSingleton): the shadow VS
			// only consumes Position (location 0), but the buffer stride must match the Vertex struct.
			VertexLayoutDesc vertexLayout{};
			VertexBufferLayoutDesc vb{};
			vb.Binding = 0;
			vb.InputRate = VertexInputRate::PerVertex;
			vb.Stride = sizeof(Vertex);
			vb.Attributes = {
			    {.Location = 0, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Position))},
			    {.Location = 1, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Normal))},
			    {.Location = 2, .Format = VertexFormat::Float2, .Offset = static_cast<uint32_t>(offsetof(Vertex, TexCoord))},
			    {.Location = 3, .Format = VertexFormat::Float4, .Offset = static_cast<uint32_t>(offsetof(Vertex, Tangent))},
			};
			vertexLayout.Buffers = {vb};

			PipelineDesc p{};
			p.Type = PipelineType::Graphics;
			p.Shader = shader;
			p.VertexLayout = vertexLayout;
			p.ColorFormats = {}; // depth-only: no color attachment
			p.DepthFormat = depthFormat;
			// Render front faces (no special cull) into the shadow map — i.e. store what the light sees
			// first. The "second-depth" trick (front-face culling to store back faces) only works for
			// watertight meshes; Sponza has single-sided floors/curtains/arches, so back-face storage made
			// light-open surfaces sample against a nearer back face and read as falsely shadowed. Acne is
			// handled by the slope-scaled + (future) normal-offset bias in the lit shader instead.
			p.Raster.Cull = CullMode::None;
			p.DepthStencil.EnableDepthTest = true;
			p.DepthStencil.EnableDepthWrite = true;
			p.DepthStencil.DepthCompare = CompareOp::Less;
			p.DebugName = "ShadowPipeline";

			m_ShadowPipeline = Pipeline::Create(p);
			SS_CORE_ASSERT(m_ShadowPipeline, "Failed to create Shadow pipeline");
			m_ShadowDepthFormat = depthFormat;
		}

		const auto& setLayouts = m_ShadowPipeline->GetSetLayouts();
		SS_CORE_ASSERT(setLayouts.size() > 2 && setLayouts[0] && setLayouts[2], "Shadow pipeline missing set 0/2");

		m_CommandContext->BindPipeline(m_ShadowPipeline);

		// set=0 Frame: AcquireFrameSet uploads FrameCB with m_ViewProj = the light's matrix (set by the
		// preceding BeginScene), so the shadow VS transforms into light clip space.
		const Ref<DescriptorSet> frameSet = AcquireFrameSet(m_ShadowPipeline, m_FrameIndex);
		m_CommandContext->BindDescriptorSet(frameSet, 0);

		EnsureInstanceBuffer(m_FrameIndex, 0);

		auto& perFrameObjectSets = m_ObjectSets[m_ShadowPipeline.get()];
		if (perFrameObjectSets.empty())
			perFrameObjectSets.resize(Renderer::GetFramesInFlight());
		if (!perFrameObjectSets[m_FrameIndex])
		{
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Set2_Instances_Shadow";
			perFrameObjectSets[m_FrameIndex] = DescriptorSet::Create(setLayouts[2], setDesc);
			SS_CORE_ASSERT(perFrameObjectSets[m_FrameIndex], "Failed to create shadow set=2 DescriptorSet");

			BufferBinding bb{};
			bb.Buffer = m_InstanceBuffers[m_FrameIndex];
			bb.Offset = 0;
			bb.Range = 0; // whole buffer
			perFrameObjectSets[m_FrameIndex]->SetBuffer(0, bb);
			perFrameObjectSets[m_FrameIndex]->Commit();
		}
		const Ref<DescriptorSet>& objectSet = perFrameObjectSets[m_FrameIndex];

		// One instanced depth draw per batch, appending into the shared instance buffer at the running
		// cursor (NewFrame reset it; the camera pass appends after us). Mirrors FlushBatch's instance
		// write + draw, minus materials/bindless. The shadow pass has its own BeginScene accumulation
		// (all casters); the camera pass's BeginScene clears these batches and re-accumulates visible ones.
		for (auto& batch : m_Batches)
		{
			if (batch.Instances.empty() || !batch.Mesh)
				continue;

			const auto instanceCount = static_cast<uint32_t>(batch.Instances.size());
			const uint32_t firstInstance = m_InstanceWriteCursor;
			if (firstInstance + instanceCount > m_InstanceBufferCapacity)
			{
				SS_CORE_ERROR("Instance buffer overflow in shadow pass ({0}+{1} > {2}); dropping batch.",
				              firstInstance, instanceCount, m_InstanceBufferCapacity);
				continue;
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
		}
	}

	void RendererSingleton::FlushBatch(BatchData& batch,
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
		// with firstInstance = sliceStart, so SV_InstanceID indexes the right entries. The descriptor
		// binds the whole buffer once (fixed capacity → committed once, never re-bound mid-frame).
		EnsureInstanceBuffer(frameIndex, static_cast<uint32_t>(batch.Instances.size()));

		auto& perFrameObjectSets = m_ObjectSets[pipeline.get()];
		if (perFrameObjectSets.empty())
			perFrameObjectSets.resize(Renderer::GetFramesInFlight());

		if (!perFrameObjectSets[frameIndex])
		{
			DescriptorSetDesc setDesc{};
			setDesc.DebugName = "Set2_Instances";
			perFrameObjectSets[frameIndex] = DescriptorSet::Create(setLayouts[2], setDesc);
			SS_CORE_ASSERT(perFrameObjectSets[frameIndex], "Failed to create set=2 Object DescriptorSet");

			BufferBinding bb{};
			bb.Buffer = m_InstanceBuffers[frameIndex];
			bb.Offset = 0;
			bb.Range = 0; // whole buffer
			perFrameObjectSets[frameIndex]->SetBuffer(0, bb);
			perFrameObjectSets[frameIndex]->Commit();
		}

		const Ref<DescriptorSet>& objectSet = perFrameObjectSets[frameIndex];

		// Write this batch's instances into the frame buffer at the running cursor.
		const auto instanceCount = static_cast<uint32_t>(batch.Instances.size());
		const uint32_t firstInstance = m_InstanceWriteCursor;
		if (firstInstance + instanceCount > m_InstanceBufferCapacity)
		{
			SS_CORE_ERROR("Instance buffer overflow ({0}+{1} > {2}); dropping batch.", firstInstance, instanceCount, m_InstanceBufferCapacity);
			batch.Instances.clear();
			return;
		}

		m_InstanceBuffers[frameIndex]->SetData(batch.Instances.data(),
		                                       instanceCount * sizeof(InstanceData),
		                                       static_cast<size_t>(firstInstance) * sizeof(InstanceData));
		m_InstanceWriteCursor += instanceCount;

		// Bind mesh buffers + set=2, then one instanced draw for the whole batch.
		commandContext->BindVertexBuffer(batch.Mesh->GetVertexBuffer(), 0, 0);
		commandContext->BindDescriptorSet(objectSet, 2);

		commandContext->DrawIndexed(batch.Mesh->GetIndexBuffer(),
		                            batch.Mesh->GetIndexCount(),
		                            instanceCount,
		                            0,
		                            0,
		                            firstInstance);

		batch.Instances.clear();
	}

	void RendererSingleton::EnsureInstanceBuffer(const uint32_t frameIndex, uint32_t /*additionalNeeded*/)
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
