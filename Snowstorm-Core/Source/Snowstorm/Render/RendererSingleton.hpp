#pragma once

#include "CommandContext.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
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

		// Bake the IBL maps (irradiance + later prefiltered/BRDF) from the current procedural-sky
		// environment, on compute. Lazy: bakes once, then on environment change. Call inside a recording
		// command buffer (before render passes). The generated cubes' bindless indices feed FrameCB.
		void BakeIBL(const Ref<CommandContext>& commandContext);

		// One-shot compute self-test (Phase 2 / #17-#18): lazily create a trivial compute pipeline, bind
		// and dispatch it once in the given command context, and log the result. The pipeline is owned by
		// this singleton so it tears down in the normal device-shutdown order (not at static-exit, which
		// would destroy GPU objects after the device is gone). Call inside a recording command buffer.
		void RunComputeSelfTest(const Ref<CommandContext>& commandContext);

		// Draw the procedural sky as a fullscreen triangle at the far plane. Call inside an active scene
		// pass (between BeginScene/EndScene) AFTER opaque meshes, so depth is populated and the sky only
		// fills uncovered pixels. Lazily builds its pipeline against the given color/depth formats.
		void DrawSky(PixelFormat colorFormat, PixelFormat depthFormat);

		// Set the directional shadow data the lit pass needs: the light's view-projection (world -> light
		// clip) and the bindless index of the shadow depth texture (0 = no shadows). The camera pass's
		// FrameCB picks these up so DefaultLit can reproject + compare. Call before the camera Flush().
		void SetShadowData(const glm::mat4& lightViewProj, uint32_t shadowMapIndex);

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

		// Lazily-built procedural sky pipeline (no vertex buffer; draws at the far plane). Built once for
		// the scene target's color/depth formats; rebuilt only if those change.
		// One-shot compute self-test (Phase 2-3 diagnostic): pipeline + a storage image it writes + the
		// UAV descriptor set. Owned here for correct teardown order (destruct before the device dies).
		Ref<Pipeline> m_ComputeSelfTestPipeline;
		Ref<Texture> m_ComputeSelfTestImage;
		Ref<TextureView> m_ComputeSelfTestView;
		Ref<DescriptorSet> m_ComputeSelfTestSet;

		// --- IBL bake (compute) ---
		// Generated from the procedural sky into HDR cubes. Owned here so they survive across frames and
		// tear down in the correct device-shutdown order. Their bindless indices feed FrameCB (Phase 6).
		Ref<Pipeline> m_IBLCapturePipeline;    // sky -> env cube
		Ref<Pipeline> m_IBLIrradiancePipeline; // env cube -> irradiance cube
		Ref<Pipeline> m_IBLPrefilterPipeline;  // env cube -> prefiltered (roughness mips) cube
		Ref<Pipeline> m_IBLBRDFLutPipeline;    // BRDF integration LUT (2D)
		Ref<Texture> m_EnvCube;                // captured sky environment (HDR)
		Ref<TextureView> m_EnvCubeView;        // full-cube sampled view (kept alive; bound during convolution)
		Ref<Texture> m_IrradianceCube;         // diffuse irradiance
		Ref<TextureView> m_IrradianceCubeView; // full-cube sampled view (kept alive; read in Phase 6)
		Ref<Texture> m_PrefilteredCube;        // specular prefiltered env (mip = roughness)
		Ref<TextureView> m_PrefilteredCubeView;
		Ref<Texture> m_BRDFLut; // 2D BRDF integration LUT
		Ref<TextureView> m_BRDFLutView;
		Ref<Sampler> m_IBLSampler; // linear clamp sampler for the convolution
		bool m_IBLBaked = false;   // bake once (regenerate on environment change later)
		// Per-face UBOs / descriptor sets / face views recorded into the bake command buffer; they must
		// outlive the in-flight frame, so the bake parks them here. Cleared after the bake is consumed.
		std::vector<Ref<void>> m_IBLBakeKeepAlive;

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
