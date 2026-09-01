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
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Snowstorm
{
	// Per-instance GPU record uploaded to the set=2 StructuredBuffer, indexed by SV_InstanceID.
	// Layout MUST match the HLSL InstanceData struct in MeshInput.hlsli exactly (std430-style).
	struct InstanceData
	{
		glm::mat4 Model{1.0f};
		glm::mat4 PrevModel{1.0f};       // last frame's world matrix, used for motion vectors (#44)
		uint32_t AlbedoTextureIndex = 0; // per-instance albedo override (0 = material default)
		glm::vec3 _Pad0{0.0f};
		// Generic per-instance custom data (cf. Unreal PerInstanceCustomData): four free floats the shader
		// interprets however it likes. Engine-neutral, since a client shader gives them meaning (the Mandelbrot
		// demo packs center.xy / zoom / iteration count). Zero for objects that don't use it.
		glm::vec4 PerInstanceCustomData{0.0f};
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
	// objects; both are the headline numbers for diagnosing draw-submission cost.
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
		// forceRasterShadow (#118): when true, this pass's FrameCB.RTShadowEnabled is forced to 0 (raster
		// shadow map) regardless of the render.shadows.rt CVar. The compare-mode ground-truth pass sets it so
		// the RT-shadows A/B metric compares the RT main render against a raster reference in one frame.
		void BeginScene(const CameraRuntimeComponent& cameraRt,
		                const glm::vec3& cameraWorldPosition,
		                const Ref<CommandContext>& commandContext,
		                uint32_t frameIndex,
		                bool useJitteredProjection = false,
		                bool forceRasterShadow = false);

		void EndScene();

		// Submit one renderable. Per-instance albedo index (0 = material default) and extras travel in
		// the instance buffer so objects sharing (mesh, material) batch into a single instanced draw.
		// prevTransform is last frame's world matrix (for motion vectors, #44); defaults to `transform`
		// (zero velocity) so callers that don't track it (shadow/velocity-agnostic paths) stay correct.
		void DrawMesh(const glm::mat4& transform,
		              const Ref<Mesh>& mesh,
		              const Ref<MaterialInstance>& materialInstance,
		              uint32_t albedoTextureIndex = 0,
		              const glm::vec4& perInstanceCustomData = glm::vec4(0.0f),
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
		// no set 0. Instances are appended at the running cursor (NOT cleared: the camera pass owns clearing).
		void DrawBatchesDepthOnly(const Ref<Pipeline>& depthPipeline, const glm::mat4& lightViewProj);

		// Draw the currently-accumulated batches through a velocity pipeline (VelocityPass, #44), emitting
		// per-pixel screen-space motion. Like DrawBatchesDepthOnly but pushes BOTH matrices (viewProj and
		// prevViewProj in a 128-byte vertex push constant, matching Velocity.vert.hlsl), and the pipeline has
		// a color attachment (RGBA16F velocity) plus its own depth. Instances carry Model + PrevModel (from
		// the set=2 buffer), so the vertex stage projects each vertex in both frames. Instances appended at
		// the running cursor (NOT cleared, since the caller's own BeginScene accumulation owns clearing).
		void DrawBatchesVelocity(const Ref<Pipeline>& velocityPipeline,
		                         const glm::mat4& viewProj,
		                         const glm::mat4& prevViewProj);

		// Draw the accumulated batches through the depth+normal prepass pipeline (#124). Like
		// DrawBatchesDepthOnly (set 2 instances), but ALSO binds `samplerSet` at set 1 (a plain sampler, pass-
		// owned) + the bindless table (set 3), and pushes per-batch alpha-mask params in an 80-byte push
		// constant (VP + albedo bindless index + mask flag + cutoff + base alpha) so the fragment stage can
		// clip cutout geometry (Sponza plants/vines), or the GI G-buffer gets a phantom solid quad.
		// Deliberately does NOT bind MaterialInstance's descriptor set (its set-1 layout differs from this
		// pipeline's -> layout-incompatibility device loss); the albedo index + cutoff ride the push constant
		// instead. No set 0 (FrameCB). Instances appended at the running cursor (NOT cleared: camera owns it).
		void DrawBatchesDepthNormal(const Ref<Pipeline>& depthNormalPipeline, const glm::mat4& viewProj,
		                            const Ref<DescriptorSet>& samplerSet);

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

		// Bindless index of the point (omni) shadow atlas (0 = points unshadowed). Per-light 6-face matrices
		// + tile rects travel inside the GPUPointShadow entries; this is the one shared texture index the
		// shader needs. Mirrors SetSpotShadowAtlasIndex.
		void SetPointShadowAtlasIndex(const uint32_t index) { m_FrameData.Shadow.PointShadowAtlasIndex = index; }

		// CPU-side directional-sun shadow fit, computed by LightingSystem (PreRender) and consumed by
		// RenderSystem's directional shadow pass. This is the sun analogue of the per-spot fit that
		// LightingSystem already bakes into GPUSpotLight (ComputeSpotViewProj + atlas tile): ALL shadow
		// *setup* (which light casts, the light-space view-proj) now lives in one place, and RenderSystem
		// only binds the depth resource + records the pass. Not part of the GPU FrameData, since it's a plain
		// handoff (the matrix reaches the shader via SetShadowData once RenderSystem has the map's index).
		struct SunShadowFit
		{
			bool Valid = false; // sun exists, casts, shadows enabled, and the scene has renderable bounds
			glm::mat4 LightViewProj{1.0f};
		};
		void SetSunShadowFit(const SunShadowFit& fit) { m_SunShadowFit = fit; }
		[[nodiscard]] const SunShadowFit& GetSunShadowFit() const { return m_SunShadowFit; }

		// Set the baked IBL data the lit pass needs: bindless indices of the irradiance + prefiltered cubes
		// and the BRDF LUT, plus the prefiltered mip count (drives the roughness->lod map). All zero = IBL
		// off (DefaultLit falls back to the analytic hemisphere ambient). The bake pass owns the maps and
		// pushes these each frame (mirrors SetShadowData); FrameCB picks them up in AcquireFrameSet.
		void SetIBLData(uint32_t irradianceIndex, uint32_t prefilteredIndex, uint32_t brdfLutIndex, uint32_t prefilteredMipCount);

		// GPU device address of the per-instance reflection geometry table (RT reflections, #118). Pushed each
		// frame by RenderSystem from the ReflectionGeometrySingleton that TlasBuildSystem fills; folded into
		// FrameCB so DefaultLit's reflection trace can resolve a committed hit to a surface via
		// vk::RawBufferLoad. 0 = no table this frame (reflection falls back to the sky cube). Mirrors
		// SetIBLData/SetShadowData (a plain per-frame handoff, not GPU FrameData).
		void SetReflectionGeometryAddress(const uint64_t address) { m_ReflectionTableAddress = address; }
		// The per-instance geometry-table device address published this frame (0 = no table). The half-res GI
		// compute pass (#124) reads it to resolve ray hits, same as the inline reflection/GI path via FrameCB.
		[[nodiscard]] uint64_t GetReflectionGeometryAddress() const { return m_ReflectionTableAddress; }

		// Half-res GI consumption (#124): the ForwardEffect sets the full-res upsampled GI target's bindless
		// index (0 = no GI this frame) + the scene target's pixel size before the forward pass, and
		// AcquireFrameSet folds both into FrameCB so DefaultLit samples the GI by screen UV. Reset to 0 each
		// frame by the effects that don't set it, so a stale index can't leak GI into a non-GI viewport.
		void SetGITexture(const uint32_t bindlessIndex, const glm::vec2& renderTargetSize)
		{
			m_GITextureIndex = bindlessIndex;
			m_GIRenderTargetSize = renderTargetSize;
		}

		// Half-res AO consumption (#126): mirror of SetGITexture. The forward shader samples the full-res
		// upsampled AO target by screen UV (same RenderTargetSize divide as GI), so this shares the render-
		// target-size push. 0 = no AO this frame. Reset to 0 per frame by non-AO viewports.
		void SetAOTexture(const uint32_t bindlessIndex, const glm::vec2& renderTargetSize)
		{
			m_AOTextureIndex = bindlessIndex;
			m_GIRenderTargetSize = renderTargetSize;
		}

		// Full-res RT reflection consumption (#129): mirror of SetGITexture. The forward shader samples the
		// full-res reflection target by screen UV and blends it into the specular term (replacing the old
		// inline RayTraceReflection). The buffer is full-res, so no size divide is needed, but it shares the
		// RenderTargetSize push for a uniform UV convention. 0 = no RT reflection this frame; reset to 0 per
		// frame by non-reflection viewports so a stale index can't leak.
		void SetReflTexture(const uint32_t bindlessIndex, const glm::vec2& renderTargetSize)
		{
			m_ReflectionTextureIndex = bindlessIndex;
			m_GIRenderTargetSize = renderTargetSize;
		}

		// Half-res RT sun-shadow consumption: mirror of SetAOTexture. The forward shader samples the full-res
		// upsampled sun-visibility target by the same screen UV (shares the RenderTargetSize divide). 0 = no
		// half-res shadow this frame -> DefaultLit falls back to the inline SampleSunShadow. Reset to 0 per
		// frame by non-shadow viewports so a stale index can't leak.
		void SetShadowTexture(const uint32_t bindlessIndex, const glm::vec2& renderTargetSize)
		{
			m_ShadowTextureIndex = bindlessIndex;
			m_GIRenderTargetSize = renderTargetSize;
		}

		// Demodulated specular twin index (shares the shadow screen-UV divide). 0 = forward uses the grey-vis
		// specular fallback. Reset per frame by non-shadow viewports, like SetShadowTexture.
		void SetShadowSpecTexture(const uint32_t bindlessIndex) { m_ShadowSpecTextureIndex = bindlessIndex; }

		// Current frame's lights / environment (uploaded by the PreRender systems). The IBL bake reads
		// these to capture the sky; exposed so the bake lives in its own pass, not the renderer.
		[[nodiscard]] const LightDataBlock& GetLights() const { return m_FrameData.Lights; }
		[[nodiscard]] const EnvironmentDataBlock& GetEnvironment() const { return m_FrameData.Environment; }

		// The whole assembled per-frame input block (camera + lights + environment + shadow + IBL). Exposed
		// for compute passes that need a cross-section of it: the half-res GI pass (#124) reads camera VP,
		// the sun, and the IBL indices together to populate its own params CB (rather than threading a
		// half-dozen separate getters). Read-only snapshot for the current frame.
		[[nodiscard]] const FrameData& GetFrameData() const { return m_FrameData; }

		// Stats from the most recently submitted scene pass (reset each BeginScene).
		[[nodiscard]] const RenderStats& GetStats() const { return m_Stats; }

		// Monotonic frame counter, incremented once per NewFrame(). Drives the temporal jitter Halton index
		// (#44); a general "which frame is this" primitive for any frame-phased effect. 64-bit, so it
		// never wraps.
		[[nodiscard]] uint64_t GetFrameCounter() const { return m_FrameCounter; }

		// True when the device supports + enabled inline ray tracing (#118). Gates the RT shadow path; the RT
		// systems/passes query this to decide whether to build AS / run the RT pass, and fall back to raster
		// when false. Forwards to Renderer (device capability).
		[[nodiscard]] bool IsRayTracingSupported() const { return Renderer::IsRayTracingSupported(); }

		// Per-pass GPU scopes (name, ms, nesting depth) resolved this frame from the graph's timestamp scopes.
		// Set by RenderSystem each frame; read by the editor overlay. Empty if the device lacks timestamps.
		void SetGpuPassTimes(std::vector<GpuScope> scopes) { m_GpuPassTimes = std::move(scopes); }
		[[nodiscard]] const std::vector<GpuScope>& GetGpuPassTimes() const { return m_GpuPassTimes; }

		// What the presented viewport rendered this frame: pixel extent, the pose of the camera that drove it,
		// and where along the scripted route that camera was. Set by RenderSystem alongside the pass times.
		//
		// Two consumers, both needing the same answer to "what was actually on screen". The perf benchmark
		// stamps size and pose into its JSON so a ms number can be rejected when the baseline was captured at
		// a different size or viewpoint (neither is pinnable by a CVar, since the render size follows the
		// host's viewport). The motion quality capture keys off PathFrame, because a route frame index is the
		// only clock a separate reference run can reproduce.
		struct FrameViewInfo
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			glm::vec3 CameraPosition{};
			glm::vec3 CameraRotation{}; // Euler radians
			uint64_t PathFrame = 0;     // route-local frame index; meaningless unless PathActive
			bool PathActive = false;    // the scripted route is on AND has started (scene resident)
		};
		void SetFrameViewInfo(const FrameViewInfo& v) { m_FrameViewInfo = v; }
		[[nodiscard]] const FrameViewInfo& GetFrameViewInfo() const { return m_FrameViewInfo; }

		// Upscaled-vs-ground-truth image-quality metrics (#45). Set by RenderSystem from the MetricsPass each
		// frame while render.metrics is on; read by the editor Performance panel + the headless metrics log.
		struct MetricsResult
		{
			bool Valid = false; // false until the first metric frame completes
			float Psnr = 0.0f;  // dB (higher = closer; capped at 100 for identical)
			float Ssim = 0.0f;  // [0,1] (1 = identical)
		};
		void SetMetrics(const MetricsResult& m) { m_Metrics = m; }
		[[nodiscard]] const MetricsResult& GetMetrics() const { return m_Metrics; }

		// Number of dataset-export tuples written to disk so far (#46). Set by RenderSystem from the
		// DatasetExportPass while dataset.export is on; read by the app loop to stop after N frames and by the
		// editor panel for a progress readout.
		void SetDatasetFramesWritten(const uint64_t n) { m_DatasetFramesWritten = n; }
		[[nodiscard]] uint64_t GetDatasetFramesWritten() const { return m_DatasetFramesWritten; }

		// 1 once the headless quality capture (#153, quality.capture.frames) has written its .npy to disk. Set
		// by RenderSystem from the QualityCapturePass; read by the app loop to exit after the single capture.
		void SetQualityCaptureWritten(const uint64_t n) { m_QualityCaptureWritten = n; }
		[[nodiscard]] uint64_t GetQualityCaptureWritten() const { return m_QualityCaptureWritten; }

		// Whether the capture has produced everything it was asked for, which is when the app may exit. Not
		// the same as "wrote something": a motion capture writes many frames and is done only after the last
		// one, so the frame count alone cannot say when to stop.
		void SetQualityCaptureComplete(const bool c) { m_QualityCaptureComplete = c; }
		[[nodiscard]] bool IsQualityCaptureComplete() const { return m_QualityCaptureComplete; }

		// --- RT editor picking (#118 follow-up) --------------------------------------------------------
		// The editor requests a pixel-accurate mesh pick by handing over the camera->cursor WORLD ray; a
		// single-thread compute dispatch traces it against the scene TLAS (RecordPick, driven by RenderSystem)
		// and latches the committed instance's custom index (TlasBuildSystem's per-entity build order), which
		// the editor maps back to an entt::entity via TlasInstanceMapSingleton. The GPU result reads back with
		// a frames-in-flight lag (same as MetricsPass), so the selection lands a frame or two after the click.
		// This service stays entity-agnostic: it deals only in the raw uint index.

		// Queue a pick. `tMax` bounds the trace (default = camera far-ish; the editor passes the far distance).
		// Latest-wins: a new request before the previous dispatches overwrites it. No-op sink on a non-RT
		// device (the editor only calls this when RT is active).
		void RequestPick(const glm::vec3& worldOrigin, const glm::vec3& worldDir, float tMax = 1e5f);

		// True while a queued ray hasn't been dispatched yet. RenderSystem checks this to decide whether to
		// add the pick compute pass this frame.
		[[nodiscard]] bool HasPendingPick() const { return m_PickRequest.Pending; }

		// CPU-only, called by RenderSystem every frame right after Renderer::BeginFrame() (which waited on this
		// frame-slot's fence, so a dispatch recorded framesInFlight frames ago into this same slot has retired
		// and its host-visible write is now readable). Maps + latches that result into m_PickResult. Separate
		// from RecordPick because the read-back must happen on the recurrence of the slot, which is generally a
		// DIFFERENT frame than the one that queued the pick; folding it into the (pending-only) GPU pass would
		// strand a single click's result forever.
		void PumpPickReadback(uint32_t frameIndex);

		// Dispatch the pending ray (if any) into this frame's slot. Called by RenderSystem inside a compute
		// graph pass, AFTER the TLAS is built and its bindless slot written this frame. Binds the pick pipeline
		// + set 0 (result + params) + BindGlobalResources (set 3 = the bindless SceneTLAS) + Dispatch(1,1,1).
		// Pass the context the render graph handed this pass, not Renderer::GetGraphicsCommandContext():
		// fork/join splits a frame's graphics commands across several command buffers, so the global
		// accessor names the currently open segment, not necessarily this pass's.
		void RecordPick(CommandContext& commandContext, uint32_t frameIndex);

		// Take the most recent completed pick result: the committed instance index, or 0xFFFFFFFF on a miss
		// (nothing under the cursor). Returns nullopt until a dispatch has read back. Clears on read.
		[[nodiscard]] std::optional<uint32_t> TryConsumePickResult();

		// Dynamic textures: a system that regenerates texels on the CPU each frame (video, a procedural
		// surface, an embedded interpreter's framebuffer) queues the copy here instead of calling
		// Texture::SetData, which submits and blocks on a fence per call. Drained into one transfer pass at
		// the top of the frame graph, so the texels are in place before any pass samples them. Same
		// request/record split as the pick above, and for the same reason: a system runs outside the graph
		// and has no command buffer of its own.
		//
		// The caller owns `src` and must not overwrite it until this frame's fence has retired, which means
		// one buffer per frame-in-flight. Queued entries are consumed every frame, so a texture that should
		// keep updating must be re-queued every frame.
		void EnqueueTextureUpload(const Ref<Buffer>& src, const Ref<Texture>& dst);

		// True when any upload is queued. RenderSystem checks this to decide whether to add the pass at all.
		[[nodiscard]] bool HasPendingTextureUploads() const { return !m_PendingTextureUploads.empty(); }

		// Record every queued copy into this frame's command buffer and clear the queue. Called by
		// RenderSystem inside a graph pass. Takes the context the graph handed the pass, not
		// Renderer::GetGraphicsCommandContext(), for the reason RecordPick documents above.
		void RecordTextureUploads(CommandContext& commandContext);

	private:
		// Create the pick compute pipeline + per-frame-in-flight result/param buffers on first use. The result
		// buffer is host-visible Storage (the shader writes it, the CPU maps it); the param buffer is a small
		// Uniform holding the ray. Returns false if the shader hasn't finished async-compiling yet.
		bool EnsurePickResources();

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
		// caller. Returns false (and logs) if the batch would overflow the buffer; the caller decides
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

		// CPU-side sun shadow fit produced by LightingSystem, consumed by RenderSystem's directional shadow
		// pass (see SunShadowFit above). Not GPU FrameData, just a plain per-frame handoff between the
		// two systems.
		SunShadowFit m_SunShadowFit{};

		// GPU device address of this frame's RT reflection geometry table (#118); pushed by
		// SetReflectionGeometryAddress, read into FrameCB in AcquireFrameSet. 0 = no table.
		uint64_t m_ReflectionTableAddress = 0;

		// Half-res GI consumption (#124): the full-res upsampled GI target's bindless index (0 = no GI) + the
		// scene target's pixel size, pushed per-viewport by SetGITexture, read into FrameCB in AcquireFrameSet.
		uint32_t m_GITextureIndex = 0;
		glm::vec2 m_GIRenderTargetSize{0.0f, 0.0f};

		// Half-res AO consumption (#126): the full-res upsampled AO target's bindless index (0 = no AO), pushed
		// per-viewport by SetAOTexture, read into FrameCB in AcquireFrameSet. Shares m_GIRenderTargetSize.
		uint32_t m_AOTextureIndex = 0;

		// Full-res RT reflection consumption (#129): the reflection target's bindless index (0 = no RT
		// reflection), pushed per-viewport by SetReflTexture, read into FrameCB in AcquireFrameSet. Shares
		// m_GIRenderTargetSize.
		uint32_t m_ReflectionTextureIndex = 0;

		// Half-res RT sun-shadow consumption: the full-res upsampled sun-visibility target's bindless index
		// (0 = no half-res shadow), pushed per-viewport by SetShadowTexture, read into FrameCB in
		// AcquireFrameSet. Shares m_GIRenderTargetSize.
		uint32_t m_ShadowTextureIndex = 0;
		uint32_t m_ShadowSpecTextureIndex = 0; // demodulated specular twin (0 = grey-vis specular fallback)

		std::vector<BatchData> m_Batches;

		// Index into m_Batches keyed by the (mesh, material-instance) raw-pointer pair, so DrawMesh finds an
		// existing batch in O(1) instead of a linear scan. Without this, N unique-material draws cost O(N^2)
		// in batch matching (measured: ~11ms of superlinear overhead at 10k unique draws). Rebuilt each
		// frame alongside m_Batches (both cleared in BeginScene). Exact pair key (not a packed hash) so
		// distinct pairs that hash-collide still compare unequal, which rules out wrong-batch merges.
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

		uint64_t m_FrameCounter = 0;      // monotonic; ++ per NewFrame() (temporal jitter index, #44)
		float m_MipBias = 0.0f;           // texture mip-LOD bias for the current scene pass (TAA, #44)
		glm::vec2 m_JitterUv{0.0f, 0.0f}; // TAA jitter (UV units) for the current pass; 0 unless jittered
		bool m_ForceRasterShadow = false; // this pass forces raster shadows (compare GT render, #118)

		RenderStats m_Stats{};

		// Per-pass GPU scopes from the most recent frame's timestamp scopes; see SetGpuPassTimes.
		std::vector<GpuScope> m_GpuPassTimes;

		// Presented viewport extent + camera pose for this frame; see SetFrameViewInfo.
		FrameViewInfo m_FrameViewInfo;

		// Latest upscaled-vs-ground-truth image metrics (#45), set by RenderSystem when render.metrics is on.
		MetricsResult m_Metrics;

		// Running count of dataset-export tuples written to disk (#46), set by RenderSystem when exporting.
		uint64_t m_DatasetFramesWritten = 0;

		// 1 once the headless quality capture (#153) has written its .npy (set by RenderSystem).
		uint64_t m_QualityCaptureWritten = 0;
		bool m_QualityCaptureComplete = false;

		// --- RT editor picking (#118 follow-up) ---
		// One pending ray, latest-wins (RequestPick overwrites). Cleared when RecordPick dispatches it.
		struct PickRequest
		{
			bool Pending = false;
			glm::vec3 Origin{0.0f};
			glm::vec3 Dir{0.0f, 0.0f, -1.0f};
			float TMax = 1e5f;
		};
		PickRequest m_PickRequest;

		Ref<Pipeline> m_PickPipeline;                 // 1-thread inline-RayQuery trace (Pick.comp.hlsl)
		std::vector<Ref<Buffer>> m_PickResultBuffers; // host-visible, per frame-in-flight; [0] = instance idx
		std::vector<Ref<Buffer>> m_PickParamBuffers;  // per frame-in-flight ray UBO
		std::vector<Ref<DescriptorSet>> m_PickSets;   // per frame-in-flight set 0 (result + params)
		std::vector<bool> m_PickDispatched;           // this slot has an in-flight/retired dispatch to read
		std::optional<uint32_t> m_PickResult;         // latest completed result, consumed by the editor

		struct TextureUpload
		{
			Ref<Buffer> Src;
			Ref<Texture> Dst;
		};
		std::vector<TextureUpload> m_PendingTextureUploads;
	};
}
