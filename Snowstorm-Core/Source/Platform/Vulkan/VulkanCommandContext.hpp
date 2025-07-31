#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/CommandContext.hpp"

namespace Snowstorm 
{
	class VulkanCommandContext : public CommandContext
	{
	public:
		VulkanCommandContext();
		~VulkanCommandContext() override;

		void BeginRenderPass(const RenderTarget& target) override;
		void EndRenderPass() override;

		void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f) override;
		void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		void BindPipeline(const Ref<Pipeline>& pipeline) override;
		void BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, uint32_t setIndex) override;
		void BindVertexBuffer(const Ref<Buffer>& vertexBuffer, uint32_t binding = 0, uint64_t offset = 0) override;
		void PushConstants(const void* data, uint32_t size, uint32_t offset = 0) override;

		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0) override;
		void DrawIndexed(const Ref<Buffer>& indexBuffer, uint32_t indexCount, uint32_t instanceCount = 1,
						 uint32_t firstIndex = 0, int32_t vertexOffset = 0) override;

		void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
		void Submit() override;

	private:
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		bool m_InRenderPass = false;

		void AllocateCommandBuffer();
	};
}
