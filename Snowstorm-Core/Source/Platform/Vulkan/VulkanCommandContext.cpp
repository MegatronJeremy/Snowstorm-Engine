#include "VulkanCommandContext.hpp"
#include "VulkanCommon.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanDescriptorSet.hpp"
#include "VulkanBuffer.hpp"

namespace Snowstorm
{
	VulkanCommandContext::VulkanCommandContext()
	{
		// Empty; VulkanCommon provides global access
		AllocateCommandBuffer();
	}

	VulkanCommandContext::~VulkanCommandContext()
	{
		vkFreeCommandBuffers(VulkanCommon::GetVulkanDevice(), m_CommandPool, 1, &m_CommandBuffer);
		vkDestroyCommandPool(VulkanCommon::GetVulkanDevice(), m_CommandPool, nullptr);
	}

	void VulkanCommandContext::AllocateCommandBuffer()
	{
		VkDevice device = VulkanCommon::GetVulkanDevice();

		VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.queueFamilyIndex = VulkanCommon::GetGraphicsQueueFamilyIndex();
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool);

		VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		vkAllocateCommandBuffers(device, &allocInfo, &m_CommandBuffer);

		VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
	}

	void VulkanCommandContext::BeginRenderPass(const RenderTarget& target)
	{
		VkRenderingAttachmentInfoKHR colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
		colorAttachment.imageView = target.GetColorAttachmentView();
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue = target.GetClearValue();

		VkRenderingInfoKHR renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
		renderingInfo.renderArea = {{0, 0}, target.GetExtent()};
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
		m_InRenderPass = true;
	}

	void VulkanCommandContext::EndRenderPass()
	{
		if (m_InRenderPass)
		{
			vkCmdEndRendering(m_CommandBuffer);
			m_InRenderPass = false;
		}
	}

	void VulkanCommandContext::SetViewport(const float x, const float y, const float width, const float height,
	                                       const float minDepth, const float maxDepth)
	{
		VkViewport viewport{x, y, width, height, minDepth, maxDepth};
		vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
	}

	void VulkanCommandContext::SetScissor(const uint32_t x, const uint32_t y, const uint32_t width,
	                                      const uint32_t height)
	{
		VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
		vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
	}

	void VulkanCommandContext::BindPipeline(const Ref<Pipeline>& pipeline)
	{
		auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(pipeline);
		vkCmdBindPipeline(m_CommandBuffer, vkPipeline->GetBindPoint(), vkPipeline->GetHandle());
	}

	void VulkanCommandContext::BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, const uint32_t setIndex)
	{
		auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
		vkCmdBindDescriptorSets(m_CommandBuffer,
		                        VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        vkSet->GetLayout(),
		                        setIndex,
		                        1, &vkSet->GetHandle(),
		                        0, nullptr);
	}

	void VulkanCommandContext::BindVertexBuffer(const Ref<Buffer>& vertexBuffer, const uint32_t binding,
	                                            const uint64_t offset)
	{
		auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(vertexBuffer);
		VkBuffer buffer = vkBuffer->GetHandle();
		VkDeviceSize offsets[] = {offset};
		vkCmdBindVertexBuffers(m_CommandBuffer, binding, 1, &buffer, offsets);
	}

	void VulkanCommandContext::PushConstants(const void* data, const uint32_t size, const uint32_t offset)
	{
		// You may need to pass pipeline layout and shader stage externally
		// Replace these with actual values
		vkCmdPushConstants(m_CommandBuffer, /*pipelineLayout*/{}, VK_SHADER_STAGE_ALL, offset, size, data);
	}

	void VulkanCommandContext::Draw(const uint32_t vertexCount, const uint32_t instanceCount,
	                                const uint32_t firstVertex)
	{
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, 0);
	}

	void VulkanCommandContext::DrawIndexed(const Ref<Buffer>& indexBuffer, const uint32_t indexCount,
	                                       const uint32_t instanceCount,
	                                       const uint32_t firstIndex, const int32_t vertexOffset)
	{
		const auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(indexBuffer);
		vkCmdBindIndexBuffer(m_CommandBuffer, vkBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, 0);
	}

	void VulkanCommandContext::Dispatch(const uint32_t groupX, const uint32_t groupY, const uint32_t groupZ)
	{
		vkCmdDispatch(m_CommandBuffer, groupX, groupY, groupZ);
	}

	void VulkanCommandContext::Submit()
	{
		vkEndCommandBuffer(m_CommandBuffer);

		VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_CommandBuffer;

		vkQueueSubmit(VulkanCommon::GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(VulkanCommon::GetGraphicsQueue());
	}
}
