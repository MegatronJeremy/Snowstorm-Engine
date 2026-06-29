#pragma once

#include "CommandContext.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <unordered_map>

namespace Snowstorm
{
	// Per-instance GPU record uploaded to the set=2 StructuredBuffer, indexed by SV_InstanceID.
	// Layout MUST match the HLSL InstanceData struct in Engine.hlsli exactly (std430-style).
	struct InstanceData
	{
		glm::mat4 Model{1.0f};
		uint32_t AlbedoTextureIndex = 0; // per-instance albedo override (0 = material default)
		glm::vec3 _Pad0{0.0f};
		glm::vec4 Extras0{0.0f};
	};
	static_assert(sizeof(InstanceData) == sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(glm::vec4),
	              "InstanceData layout must match HLSL (mat4 + uint+pad3 + vec4)");

	struct BatchData
	{
		Ref<Mesh> Mesh;
		Ref<MaterialInstance> MaterialInstance;
		std::vector<InstanceData> Instances;
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
		// Call once per frame, before any BeginScene, after Renderer::BeginFrame(). Resets the per-frame
		// instance write cursor so multiple passes in the frame (shadow depth pass + camera pass, or
		// several viewports) APPEND into the shared instance buffer instead of each resetting to 0 and
		// clobbering the others' already-recorded draws.
		void NewFrame();

		void BeginScene(const CameraRuntimeComponent& cameraRt,
		                const glm::vec3& cameraWorldPosition,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex);

		void EndScene();

		// Submit one renderable. Per-instance albedo index (0 = material default) and extras travel in
		// the instance buffer so objects sharing (mesh, material) batch into a single instanced draw.
		void DrawMesh(const glm::mat4& transform,
		              const Ref<Mesh>& mesh,
		              const Ref<MaterialInstance>& materialInstance,
		              uint32_t albedoTextureIndex = 0,
		              const glm::vec4& extras0 = glm::vec4(0.0f));

		void UploadLights(const LightDataBlock& lightData);

		// Scene environment (sky/ambient colors) for the current frame. Mirrors UploadLights; the values
		// are folded into FrameCB and consumed by both the sky pass and the DefaultLit ambient term.
		void UploadEnvironment(const EnvironmentDataBlock& environment);

		void Flush();

		// Draw the procedural sky as a fullscreen triangle at the far plane. Call inside an active scene
		// pass (between BeginScene/EndScene) AFTER opaque meshes, so depth is populated and the sky only
		// fills uncovered pixels. Lazily builds its pipeline against the given color/depth formats.
		void DrawSky(PixelFormat colorFormat, PixelFormat depthFormat);

		// Set the directional shadow data the lit pass needs: the light's view-projection (world -> light
		// clip) and the bindless index of the shadow depth texture (0 = no shadows). The camera pass's
		// FrameCB picks these up so DefaultLit can reproject + compare. Call before the camera Flush().
		void SetShadowData(const glm::mat4& lightViewProj, uint32_t shadowMapIndex);

		// Set the baked IBL data the lit pass needs: bindless indices of the irradiance + prefiltered cubes
		// and the BRDF LUT, plus the prefiltered mip count (drives the roughness->lod map). All zero = IBL
		// off (DefaultLit falls back to the analytic hemisphere ambient). The bake pass owns the maps and
		// pushes these each frame (mirrors SetShadowData); FrameCB picks them up in AcquireFrameSet.
		void SetIBLData(uint32_t irradianceIndex, uint32_t prefilteredIndex, uint32_t brdfLutIndex, uint32_t prefilteredMipCount);

		// Current frame's lights / environment (uploaded by the PreRender systems). The IBL bake reads
		// these to capture the sky; exposed so the bake lives in its own pass, not the renderer.
		[[nodiscard]] const LightDataBlock& GetLights() const { return m_Lights; }
		[[nodiscard]] const EnvironmentDataBlock& GetEnvironment() const { return m_Environment; }

		// Draw the currently-accumulated batches (from DrawMesh) into the bound depth-only shadow target,
		// using a dedicated depth-only pipeline and the light's matrix (m_ViewProj, set by the preceding
		// BeginScene with the light's view-projection). No materials/bindless. Call inside the shadow
		// RenderGraph pass; the batches are consumed (instances cleared) like Flush().
		void DrawShadowDepth(PixelFormat depthFormat);

		// The shared shadow-map render target (lazily created at a fixed resolution). RenderSystem uses it
		// as the shadow pass's target and reads its depth texture's bindless index for SetShadowData.
		[[nodiscard]] const Ref<RenderTarget>& GetOrCreateShadowTarget();

		// Stats from the most recently submitted scene pass (reset each BeginScene).
		[[nodiscard]] const RenderStats& GetStats() const { return m_Stats; }

	private:
		void FlushBatch(BatchData& batch,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex);

		// Acquire (creating on first use) the per-(pipeline, frame) set=0 Frame descriptor set, and
		// upload the current frame's FrameCB into its backing UBO. Shared by the mesh batches and the
		// sky pass so the FrameCB assembly (incl. InvViewProj) lives in exactly one place.
		Ref<DescriptorSet> AcquireFrameSet(const Ref<Pipeline>& pipeline, uint32_t frameIndex);

		// Ensure the per-frame instance storage buffer for `frameIndex` exists and can hold at least
		// `additionalNeeded` more elements past the current write cursor; (re)allocates if needed.
		void EnsureInstanceBuffer(uint32_t frameIndex, uint32_t additionalNeeded);

	private:
		Ref<CommandContext> m_CommandContext;
		uint32_t m_FrameIndex = 0;

		glm::mat4 m_ViewProj{1.0f};
		glm::vec3 m_CameraPosition{0.0f};
		LightDataBlock m_Lights{};
		EnvironmentDataBlock m_Environment{};

		std::vector<BatchData> m_Batches;

		// Cached per-pipeline sets, per frame-in-flight
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_FrameSets;
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_ObjectSets;

		std::unordered_map<const DescriptorSet*, Ref<Buffer>> m_FrameUniformBuffers;

		// --- IBL data (the maps themselves are owned by IBLBakePass; the renderer just stores the bindless
		// indices the bake pushes via SetIBLData, mirroring the shadow data above). 0 = IBL off. ---
		uint32_t m_IrradianceCubeIndex = 0;
		uint32_t m_PrefilteredCubeIndex = 0;
		uint32_t m_BRDFLutIndex = 0;
		uint32_t m_PrefilteredMipCount = 0;

		Ref<Pipeline> m_SkyPipeline;
		PixelFormat m_SkyColorFormat = PixelFormat::Unknown;
		PixelFormat m_SkyDepthFormat = PixelFormat::Unknown;

		// Lazily-built depth-only shadow pipeline (mesh vertex layout, no color target). Plus the current
		// frame's light view-projection + shadow-map bindless index, folded into the camera pass's FrameCB.
		Ref<Pipeline> m_ShadowPipeline;
		PixelFormat m_ShadowDepthFormat = PixelFormat::Unknown;
		glm::mat4 m_LightViewProj{1.0f};
		uint32_t m_ShadowMapIndex = 0;

		// Shared shadow-map target (depth-only, sampleable). Resolution comes from the
		// render.shadow.resolution CVar; rebuilt when it changes (see GetOrCreateShadowTarget).
		Ref<RenderTarget> m_ShadowTarget;

		// Per-frame storage buffer holding all instances for the frame (set=2). Bound once; each batch
		// indexes its slice via the draw's firstInstance (SV_InstanceID includes firstInstance). Fixed
		// capacity so the descriptor is committed once and never needs re-binding.
		std::vector<Ref<Buffer>> m_InstanceBuffers; // indexed by frame-in-flight
		uint32_t m_InstanceBufferCapacity = 0;      // in InstanceData elements
		uint32_t m_InstanceWriteCursor = 0;         // elements written this frame

		RenderStats m_Stats{};
	};
}
