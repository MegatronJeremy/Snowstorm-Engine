#pragma once

#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"

#include <string>
#include <vector>

namespace Snowstorm
{
	// A resolved per-pass GPU timing scope. Depth is the nesting level (0 = top-level pass; a scope opened
	// inside another, e.g. Sky within the Forward pass, has depth 1) so the editor can indent children. A
	// parent's Milliseconds is inclusive of its children (standard profiler convention, cf. Unreal stat gpu).
	struct GpuScope
	{
		std::string Name;
		float Milliseconds = 0.0f;
		uint32_t Depth = 0;
	};

	class CommandContext
	{
	public:
		virtual ~CommandContext() = default;

		// Dynamic rendering lifecycle
		virtual void BeginRenderPass(const RenderTarget& target) = 0;
		virtual void EndRenderPass() = 0;

		// Viewport / Scissor
		virtual void SetViewport(float x, float y, float width, float height,
		                         float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
		virtual void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

		// Pipeline and Resources
		virtual void BindPipeline(const Ref<Pipeline>& pipeline) = 0;

		virtual void BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, uint32_t setIndex) = 0;

		virtual void BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet,
		                               uint32_t setIndex,
		                               const uint32_t* dynamicOffsets,
		                               uint32_t dynamicOffsetCount) = 0;

		// Bind a CONTIGUOUS run of descriptor sets [firstSet, firstSet+count) in a single backend call
		// (one vkCmdBindDescriptorSets). Prefer this over N single-set binds when a draw's sets are known
		// together: it's the "bind the whole set table at once" pattern real engines use (Unreal/Unity),
		// fewer driver calls, and it avoids leaving graphics debuggers unsure whether an earlier-bound set
		// is still current. The sets must be non-null and land at ascending, gap-free set indices.
		virtual void BindDescriptorSets(uint32_t firstSet, const std::vector<Ref<DescriptorSet>>& sets) = 0;

		virtual void BindVertexBuffer(const Ref<Buffer>& vertexBuffer, uint32_t binding = 0, uint64_t offset = 0) = 0;

		virtual void BindGlobalResources() = 0;

		virtual void PushConstants(const void* data, uint32_t size, uint32_t offset = 0) = 0;

		// Draw commands
		virtual void Draw(uint32_t vertexCount,
		                  uint32_t instanceCount = 1,
		                  uint32_t firstVertex = 0) = 0;

		virtual void DrawIndexed(const Ref<Buffer>& indexBuffer,
		                         uint32_t indexCount,
		                         uint32_t instanceCount = 1,
		                         uint32_t firstIndex = 0,
		                         int32_t vertexOffset = 0,
		                         uint32_t firstInstance = 0) = 0;

		// Dispatch (compute support)
		virtual void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) = 0;

		// --- Image layout transitions for compute (backend-agnostic) ---
		// Move a texture to the storage/UAV layout (Vulkan GENERAL) so a compute shader can read/write it.
		virtual void TransitionToStorage(const Ref<Texture>& texture) = 0;
		// Move a texture to the shader-sampled read layout (e.g. after a compute pass wrote it, before a
		// later pass samples it). Auto-redirects to the depth-read layout for depth textures.
		virtual void TransitionToSampled(const Ref<Texture>& texture) = 0;
		// Execution+memory barrier for a graphics-color-write -> compute-sampled-read on a texture ALREADY in
		// the sampled layout (no layout change). Needed because a plain Sampled re-declaration emits no
		// barrier when the layout is unchanged, so a compute pass could read a just-written color target
		// before its writes are visible. Use before a compute pass samples a color target the same frame.
		virtual void BarrierColorWriteToComputeRead(const Ref<Texture>& texture) = 0;

		// Global compute-write -> compute-read memory barrier (a VkMemoryBarrier2 over the compute stage).
		// Chained compute dispatches that ping-pong storage buffers (e.g. the neural upscaler's conv stack:
		// layer N writes a feature buffer layer N+1 reads) have a read-after-write hazard the graph's per-pass
		// transitions don't cover, since it's all one pass. Emit this between the writing and reading Dispatch
		// so the read sees the completed write. Covers all storage buffers/images touched by compute.
		virtual void BarrierComputeStorage() = 0;

		// GPU->CPU readback: copy a texture's mip 0 / layer 0 into a host-visible buffer (created with
		// BufferUsage::Readback). Transitions the image SHADER_READ_ONLY -> TRANSFER_SRC, does a tightly-packed
		// vkCmdCopyImageToBuffer (bytes = width*height*bytesPerPixel, no row padding), then restores it to
		// SHADER_READ_ONLY so later sampling still works. The buffer must be >= that byte size. The copied bytes
		// are the image's raw texel format (e.g. RGBA16F = 8 B/texel). Map() the buffer a frame later (after the
		// submit's fence) to read — reading same-frame races the GPU. Color textures only (mip 0, layer 0).
		virtual void CopyTextureToBuffer(const Ref<Texture>& texture, const Ref<Buffer>& dst) = 0;

		// Reset the internal state between passes if the backend needs it
		virtual void ResetState() = 0;

		// --- Per-pass GPU timing (timestamp queries) ---
		// Bracket a render/compute pass with a named GPU scope: BeginGpuScope writes a start timestamp,
		// EndGpuScope an end timestamp, into a per-frame query pool. The pair is resolved one frame later
		// (when the slot's submission has finished) and reported by CollectGpuScopes. The RenderGraph wraps
		// each pass in these, giving an Unreal "stat gpu" / RDG-style per-pass breakdown. Scopes may NEST
		// (a pass can open a sub-scope, e.g. Sky inside Forward); the resolved GpuScope carries its Depth.
		// No-op on backends/devices without timestamp support (results are then empty).
		virtual void BeginGpuScope(const std::string& name) = 0;
		virtual void EndGpuScope() = 0;

		// Called once at frame start (before any scope): resolves the PREVIOUS use of this context's query
		// pool into GpuScopes (name, ms, depth) and clears the recording state for the new frame. Returns the
		// resolved scopes from that prior frame (empty if timestamps are unsupported or nothing was recorded).
		virtual std::vector<GpuScope> CollectGpuScopes() = 0;

		// --- Debug labels (VK_EXT_debug_utils / RenderDoc, PIX, Nsight) ---
		// Annotate the command stream so a graphics debugger shows named, nestable regions in its event
		// browser instead of opaque draw indices. Begin/End bracket a region (must be balanced, may nest);
		// Insert drops a one-off marker. Independent of GPU timing — the RenderGraph drives these from the
		// same per-pass scope as BeginGpuScope, but a device with no timestamp support still gets labels.
		// Default no-op: only the Vulkan backend (when debug-utils is enabled) overrides them. The color is
		// an RGBA tint the debugger uses to shade the region; components in [0,1], default a neutral grey.
		virtual void BeginDebugLabel(const std::string& /*name*/, float /*r*/ = 0.6f, float /*g*/ = 0.6f, float /*b*/ = 0.6f) {}
		virtual void EndDebugLabel() {}
		virtual void InsertDebugLabel(const std::string& /*name*/, float /*r*/ = 0.6f, float /*g*/ = 0.6f, float /*b*/ = 0.6f) {}
	};
}
