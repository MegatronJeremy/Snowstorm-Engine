#pragma once

#include "CommandContext.hpp"
#include "Material.hpp"
#include "Mesh.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/FrameData.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>

namespace Snowstorm
{
	// Per-instance GPU record uploaded to the set=2 StructuredBuffer, indexed by SV_InstanceID.
	// Layout MUST match the HLSL InstanceData struct in MeshInput.hlsli exactly (std430-style).
	struct InstanceData
	{
		glm::mat4 Model{1.0f};
		glm::mat4 PrevModel{1.0f};       // last frame's world matrix — for motion vectors (#44)
		uint32_t AlbedoTextureIndex = 0; // per-instance albedo override (0 = material default)
		glm::vec3 _Pad0{0.0f};
		glm::vec4 Extras0{0.0f};
	};
	static_assert(sizeof(InstanceData) == 2 * sizeof(glm::mat4) + sizeof(glm::vec4) + sizeof(glm::vec4),
	              "InstanceData layout must match HLSL (mat4 Model + mat4 PrevModel + uint+pad3 + vec4)");

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

	// Application-scoped renderer subsystem: owns per-frame batching, descriptor-set caches, and FrameCB
	// assembly for the Vulkan device. Device-lifetime, shared across every World (see RegisterCoreServices).
	class RendererService final : public Service
	{
	public:
		// Call once per frame, before any BeginScene, after Renderer::BeginFrame(). Resets the per-frame
		// instance write cursor so multiple passes in the frame (shadow depth pass + camera pass, or
		// several viewports) APPEND into the shared instance buffer instead of each resetting to 0 and
		// clobbering the others' already-recorded draws.
		void NewFrame();

		// useJitteredProjection (#44): when true, FrameCB.ViewProj uses cameraRt.JitteredViewProjection (the
		// temporal sub-pixel offset) instead of the canonical ViewProjection. Only the forward COLOR pass
		// sets it; shadow/velocity/ground-truth pass false so their matrices stay geometrically true.
		void BeginScene(const CameraRuntimeComponent& cameraRt,
		                const glm::vec3& cameraWorldPosition,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex,
		                bool useJitteredProjection = false);

		void EndScene();

		// Submit one renderable. Per-instance albedo index (0 = material default) and extras travel in
		// the instance buffer so objects sharing (mesh, material) batch into a single instanced draw.
		// prevTransform is last frame's world matrix (for motion vectors, #44); defaults to `transform`
		// (zero velocity) so callers that don't track it — shadow/velocity-agnostic paths — stay correct.
		void DrawMesh(const glm::mat4& transform,
		              const Ref<Mesh>& mesh,
		              const Ref<MaterialInstance>& materialInstance,
		              uint32_t albedoTextureIndex = 0,
		              const glm::vec4& extras0 = glm::vec4(0.0f),
		              const glm::mat4& prevTransform = glm::mat4(1.0f));

		void UploadLights(const LightDataBlock& lightData);

		// Scene environment (sky/ambient colors) for the current frame. Mirrors UploadLights; the values
		// are folded into FrameCB and consumed by both the sky pass and the DefaultLit ambient term.
		void UploadEnvironment(const EnvironmentDataBlock& environment);

		void Flush();

		// Draw the currently-accumulated batches (from DrawMesh) depth-only, using the given depth pipeline
		// (owned by ShadowPass). `lightViewProj` is pushed as a per-draw push constant (the shadow VS reads
		// it from there, NOT FrameCB), so the SAME accumulated batches can be re-rendered for multiple light
		// views in one pass (the spot atlas draws each tile with a different matrix). No materials/bindless,
		// no set 0. Instances are appended at the running cursor (NOT cleared — the camera pass owns clearing).
		void DrawBatchesDepthOnly(const Ref<Pipeline>& depthPipeline, const glm::mat4& lightViewProj);

		// Draw the currently-accumulated batches through a velocity pipeline (VelocityPass, #44), emitting
		// per-pixel screen-space motion. Like DrawBatchesDepthOnly but pushes BOTH matrices — viewProj and
		// prevViewProj (128-byte vertex push constant, matching Velocity.vert.hlsl) — and the pipeline has a
		// color attachment (RGBA16F velocity) plus its own depth. Instances carry Model + PrevModel (from the
		// set=2 buffer), so the vertex stage projects each vertex in both frames. Instances appended at the
		// running cursor (NOT cleared — the caller's own BeginScene accumulation owns clearing).
		void DrawBatchesVelocity(const Ref<Pipeline>& velocityPipeline,
		                         const glm::mat4& viewProj,
		                         const glm::mat4& prevViewProj);

		// Bind the given pipeline + its set=0 Frame descriptor (FrameCB) and draw a vertex-buffer-less
		// fullscreen triangle (3 verts from SV_VertexID). Used by SkyPass; the FrameCB carries everything
		// the fullscreen shader needs (InvViewProj, environment). No-op outside an active scene pass.
		void DrawFullscreenTriangle(const Ref<Pipeline>& pipeline);

		// Per-draw tonemap push constant (mirrors TonemapPush in Tonemap.frag.hlsl field-for-field). The
		// scene-color index + debug fields travel per-draw (not via FrameCB) because compare mode records
		// the tonemap pass twice per frame and a shared UBO would leave both draws with the last-written
		// values. DebugMode 0 = normal; 1 = motion-vector view (samples DebugTexIndex, scaled by DebugScale).
		struct TonemapParams
		{
			uint32_t SceneColorIndex = 0;
			uint32_t DebugMode = 0;
			uint32_t DebugTexIndex = 0;
			float DebugScale = 1.0f;
		};

		// Post-process fullscreen draw: like DrawFullscreenTriangle but also binds the set=3 bindless table
		// (the tonemap shader samples the HDR scene color / velocity via bindless indices in the push
		// constant) and does NOT require an active BeginScene (it runs as its own graph pass with its own
		// command context + frame index). The params ride a per-draw push constant.
		void DrawPostProcess(const Ref<Pipeline>& pipeline,
		                     const Ref<CommandContext>& commandContext,
		                     uint32_t frameIndex,
		                     const TonemapParams& params);

		// Set the directional shadow data the lit pass needs: the light's view-projection (world -> light
		// clip), the bindless index of the shadow depth texture (0 = no shadows), and the shadow map's
		// resolution (for the PCF texel-size). The camera pass's FrameCB picks these up so DefaultLit can
		// reproject + compare. Pushed by ShadowPass; call before the camera Flush().
		void SetShadowData(const glm::mat4& lightViewProj, uint32_t shadowMapIndex, uint32_t shadowResolution);

		// Bindless index of the spot shadow atlas (0 = spots unshadowed). Per-spot shadow matrices + atlas
		// rects travel inside the GPUSpotLight entries; this is the one shared texture index the shader needs.
		void SetSpotShadowAtlasIndex(const uint32_t index) { m_FrameData.Shadow.SpotShadowAtlasIndex = index; }

		// Set the baked IBL data the lit pass needs: bindless indices of the irradiance + prefiltered cubes
		// and the BRDF LUT, plus the prefiltered mip count (drives the roughness->lod map). All zero = IBL
		// off (DefaultLit falls back to the analytic hemisphere ambient). The bake pass owns the maps and
		// pushes these each frame (mirrors SetShadowData); FrameCB picks them up in AcquireFrameSet.
		void SetIBLData(uint32_t irradianceIndex, uint32_t prefilteredIndex, uint32_t brdfLutIndex, uint32_t prefilteredMipCount);

		// Current frame's lights / environment (uploaded by the PreRender systems). The IBL bake reads
		// these to capture the sky; exposed so the bake lives in its own pass, not the renderer.
		[[nodiscard]] const LightDataBlock& GetLights() const { return m_FrameData.Lights; }
		[[nodiscard]] const EnvironmentDataBlock& GetEnvironment() const { return m_FrameData.Environment; }

		// Stats from the most recently submitted scene pass (reset each BeginScene).
		[[nodiscard]] const RenderStats& GetStats() const { return m_Stats; }

		// Monotonic frame counter, incremented once per NewFrame(). Drives the temporal jitter Halton index
		// (#44); a general "which frame is this" primitive for any frame-phased effect. 64-bit — never wraps.
		[[nodiscard]] uint64_t GetFrameCounter() const { return m_FrameCounter; }

		// Per-pass GPU scopes (name, ms, nesting depth) resolved this frame from the graph's timestamp scopes.
		// Set by RenderSystem each frame; read by the editor overlay. Empty if the device lacks timestamps.
		void SetGpuPassTimes(std::vector<GpuScope> scopes) { m_GpuPassTimes = std::move(scopes); }
		[[nodiscard]] const std::vector<GpuScope>& GetGpuPassTimes() const { return m_GpuPassTimes; }

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

		// Acquire (creating on first use) the per-(pipeline, frame) set=2 Object descriptor set, bound once
		// to the whole per-frame instance buffer. Shared by the lit mesh flush and the depth-only pass so
		// the descriptor-set caching lives in one place. `debugName` labels the set on first creation.
		const Ref<DescriptorSet>& AcquireObjectSet(const Ref<Pipeline>& pipeline, uint32_t frameIndex, const char* debugName);

		// Write one batch's instances into the shared instance buffer at the running cursor and record a
		// single instanced DrawIndexed. Descriptor sets (including set 2) must already be bound by the
		// caller. Returns false (and logs) if the batch would overflow the buffer — the caller decides
		// whether to clear the batch. The shared core of FlushBatch and DrawBatchesDepthOnly (the
		// instance-write + draw the depth and lit paths agree on).
		bool WriteBatchInstancedDraw(BatchData& batch, const char* overflowContext);

	private:
		Ref<CommandContext> m_CommandContext;
		uint32_t m_FrameIndex = 0;

		// All per-frame render inputs (camera, lights, environment, shadow + IBL blocks) in one struct that
		// the passes populate and AcquireFrameSet reads to build the GPU FrameCB (#72). Replaces the loose
		// per-feature scalars that used to sit directly on the service.
		FrameData m_FrameData{};

		std::vector<BatchData> m_Batches;

		// Index into m_Batches keyed by the (mesh, material-instance) raw-pointer pair, so DrawMesh finds an
		// existing batch in O(1) instead of a linear scan. Without this, N unique-material draws cost O(N^2)
		// in batch matching (measured: ~11ms of superlinear overhead at 10k unique draws). Rebuilt each
		// frame alongside m_Batches (both cleared in BeginScene). Exact pair key (not a packed hash) so
		// distinct pairs that hash-collide still compare unequal — no wrong-batch merges.
		using BatchKey = std::pair<const Mesh*, const MaterialInstance*>;
		struct BatchKeyHash
		{
			size_t operator()(const BatchKey& k) const noexcept
			{
				const auto a = reinterpret_cast<uintptr_t>(k.first);
				const auto b = reinterpret_cast<uintptr_t>(k.second);
				return std::hash<uintptr_t>{}(a) ^ (std::hash<uintptr_t>{}(b) + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
			}
		};
		std::unordered_map<BatchKey, size_t, BatchKeyHash> m_BatchIndex;

		// Cached per-pipeline sets, per frame-in-flight
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_FrameSets;
		std::unordered_map<const Pipeline*, std::vector<Ref<DescriptorSet>>> m_ObjectSets;

		std::unordered_map<const DescriptorSet*, Ref<Buffer>> m_FrameUniformBuffers;

		// Per-frame storage buffer holding all instances for the frame (set=2). Bound once; each batch
		// indexes its slice via the draw's firstInstance (SV_InstanceID includes firstInstance). Fixed
		// capacity so the descriptor is committed once and never needs re-binding.
		std::vector<Ref<Buffer>> m_InstanceBuffers; // indexed by frame-in-flight
		uint32_t m_InstanceBufferCapacity = 0;      // in InstanceData elements
		uint32_t m_InstanceWriteCursor = 0;         // elements written this frame

		uint64_t m_FrameCounter = 0; // monotonic; ++ per NewFrame() (temporal jitter index, #44)

		RenderStats m_Stats{};

		// Per-pass GPU scopes from the most recent frame's timestamp scopes; see SetGpuPassTimes.
		std::vector<GpuScope> m_GpuPassTimes;
	};
}
