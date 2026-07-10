#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/CommandContext.hpp"

#include <vector>

namespace Snowstorm
{
	class VulkanGraphicsPipeline;
	class VulkanComputePipeline;
	class Texture;

	class VulkanCommandContext final : public CommandContext
	{
	public:
		VulkanCommandContext();
		~VulkanCommandContext() override;

		void Begin();
		void End();

		VkCommandBuffer GetVulkanCommandBuffer() const { return m_CommandBuffer; }

		void TransitionLayout(const Ref<Texture>& texture, VkImageLayout newLayout) const;

		void BeginRenderPass(const RenderTarget& target) override;
		void EndRenderPass() override;

		void SetViewport(float x, float y, float width, float height,
		                 float minDepth = 0.0f, float maxDepth = 1.0f) override;
		void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		void BindPipeline(const Ref<Pipeline>& pipeline) override;

		void BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, uint32_t setIndex) override;

		void BindDescriptorSets(uint32_t firstSet, const std::vector<Ref<DescriptorSet>>& sets) override;

		void BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet,
		                       uint32_t setIndex,
		                       const uint32_t* dynamicOffsets,
		                       uint32_t dynamicOffsetCount) override;

		void BindVertexBuffer(const Ref<Buffer>& vertexBuffer,
		                      uint32_t binding = 0, uint64_t offset = 0) override;

		void BindGlobalResources() override;

		void PushConstants(const void* data, uint32_t size, uint32_t offset = 0) override;

		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
		          uint32_t firstVertex = 0) override;

		void DrawIndexed(const Ref<Buffer>& indexBuffer, uint32_t indexCount,
		                 uint32_t instanceCount = 1,
		                 uint32_t firstIndex = 0,
		                 int32_t vertexOffset = 0,
		                 uint32_t firstInstance = 0) override;

		void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;

		void TransitionToStorage(const Ref<Texture>& texture) override;
		void TransitionToSampled(const Ref<Texture>& texture) override;
		void BarrierColorWriteToComputeRead(const Ref<Texture>& texture) override;
		void CopyTextureToBuffer(const Ref<Texture>& texture, const Ref<Buffer>& dst) override;

		void ResetState() override;

		void BeginGpuScope(const std::string& name) override;
		void EndGpuScope() override;
		std::vector<GpuScope> CollectGpuScopes() override;

		void BeginDebugLabel(const std::string& name, float r, float g, float b) override;
		void EndDebugLabel() override;
		void InsertDebugLabel(const std::string& name, float r, float g, float b) override;

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

		// --- Per-pass GPU timing (timestamp queries) ---
		// One timestamp query pool owned by this context (the context is per-frame-in-flight, so the pool is
		// naturally reused only after its prior submission has retired). Each BeginGpuScope/EndGpuScope pair
		// consumes two query slots; CollectGpuScopes (at frame start) resolves the PRIOR recording's pairs to
		// milliseconds, then resets for the new frame. Capacity is fixed; scopes past it are dropped + warned.
		VkQueryPool m_TimestampPool = VK_NULL_HANDLE;
		bool m_TimestampsSupported = false;
		float m_TimestampPeriodNs = 0.0f; // ns per tick (VkPhysicalDeviceLimits::timestampPeriod)
		uint32_t m_ScopeQueryCursor = 0;  // next free query slot in the current recording
		bool m_ScopesRecorded = false;    // the pool holds a resolvable prior recording

		// One record per scope opened this recording, in Begin order. StartQuery is the query slot of its
		// start stamp (end stamp is StartQuery+1). Depth is the nesting level at Begin time. Kept until the
		// next CollectGpuScopes, which resolves them against the now-retired pool, then clears for the frame.
		struct ScopeRecord
		{
			std::string Name;
			uint32_t StartQuery = 0;
			uint32_t Depth = 0;
		};
		std::vector<ScopeRecord> m_Scopes;
		// Stack of indices into m_Scopes for scopes still open (not yet EndGpuScope'd). Its size is the
		// current nesting depth; EndGpuScope pops it to find which scope's end stamp to write.
		std::vector<uint32_t> m_OpenScopes;

		bool m_IsRendering = false;

		VkPipelineLayout m_CurrentPipelineLayout = VK_NULL_HANDLE;
		VkPipelineBindPoint m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		// Exactly one of these is set after BindPipeline, depending on the bound pipeline's type. Used by
		// PushConstants to look up the declared stage flags for a range.
		Ref<VulkanGraphicsPipeline> m_CurrentGraphicsPipeline;
		Ref<VulkanComputePipeline> m_CurrentComputePipeline;

		// Color attachments of the active render pass, and whether it targets the swapchain.
		// Used by EndRenderPass to leave offscreen color targets in SHADER_READ_ONLY so they can be
		// sampled afterwards (e.g. the editor viewport texture).
		std::vector<Ref<Texture>> m_CurrentColorTargets;
		// A sampleable depth attachment (e.g. the shadow map) to transition to shader-read after the pass.
		// Null for the normal scene depth (DepthStencil-only, never sampled). Set in BeginRenderPass.
		Ref<Texture> m_CurrentSampledDepthTarget;
		bool m_CurrentTargetIsSwapchain = false;
	};
}
