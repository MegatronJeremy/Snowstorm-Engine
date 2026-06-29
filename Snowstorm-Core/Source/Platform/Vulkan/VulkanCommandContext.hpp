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

		void ResetState() override;

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

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
