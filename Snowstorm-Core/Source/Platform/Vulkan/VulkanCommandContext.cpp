#include "VulkanCommandContext.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanRenderTarget.hpp"
#include "Platform/Vulkan/VulkanGraphicsPipeline.hpp"
#include "Platform/Vulkan/VulkanDescriptorSet.hpp"

namespace Snowstorm
{
	VulkanCommandContext::VulkanCommandContext()
	{
		const VkDevice device = VulkanCommon::GetVulkanDevice();
		const VkCommandPool pool = VulkanCommon::GetGraphicsCommandPool();

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &m_CommandBuffer);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan command buffer");
	}

	VulkanCommandContext::~VulkanCommandContext()
	{
		const VkDevice device = VulkanCommon::GetVulkanDevice();
		const VkCommandPool pool = VulkanCommon::GetGraphicsCommandPool();

		if (m_CommandBuffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(device, pool, 1, &m_CommandBuffer);
			m_CommandBuffer = VK_NULL_HANDLE;
		}
	}

	void VulkanCommandContext::Begin()
	{
		m_IsRendering = false;

		SS_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "Command buffer not initialized");

		vkResetCommandBuffer(m_CommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		const VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to begin Vulkan command buffer");
	}

	void VulkanCommandContext::End()
	{
		if (m_IsRendering)
		{
			SS_CORE_WARN("VulkanCommandContext::End called while still rendering; ending rendering automatically.");
			EndRenderPass();
		}

		const VkResult res = vkEndCommandBuffer(m_CommandBuffer);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to end Vulkan command buffer");
	}

	void VulkanCommandContext::BeginRenderPass(const RenderTarget& target)
	{
		SS_CORE_ASSERT(!m_IsRendering, "BeginRenderPass called while already rendering");

		// Dynamic rendering path
		const auto& vkTarget = dynamic_cast<const VulkanRenderTarget&>(target);

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.offset = {.x = 0, .y = 0 };
		renderingInfo.renderArea.extent = {.width = vkTarget.GetWidth(), .height = vkTarget.GetHeight() };
		renderingInfo.layerCount = 1;

		const auto& colors = vkTarget.GetColorAttachmentInfos();
		renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colors.size());
		renderingInfo.pColorAttachments = colors.empty() ? nullptr : colors.data();

		renderingInfo.pDepthAttachment = vkTarget.GetDepthAttachmentInfo();
		renderingInfo.pStencilAttachment = vkTarget.GetStencilAttachmentInfo();

		// NOTE: This does NOT insert image layout transitions. For correctness, the images must
		// already be in COLOR_ATTACHMENT_OPTIMAL / DEPTH_ATTACHMENT_OPTIMAL / etc.
		// We can add transitions once VulkanTexture exposes the current layout state (or you add a resource barrier system).
		vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
		m_IsRendering = true;

		// Common default: viewport + scissor match target size
		SetViewport(0.0f, 0.0f,
		            static_cast<float>(vkTarget.GetWidth()),
		            static_cast<float>(vkTarget.GetHeight()),
		            0.0f, 1.0f);

		SetScissor(0, 0, vkTarget.GetWidth(), vkTarget.GetHeight());
	}

	void VulkanCommandContext::EndRenderPass()
	{
		SS_CORE_ASSERT(m_IsRendering, "EndRenderPass called but no render pass is active");

		vkCmdEndRendering(m_CommandBuffer);
		m_IsRendering = false;
	}

	void VulkanCommandContext::SetViewport(const float x, const float y,
	                                       const float width, const float height,
	                                       const float minDepth, const float maxDepth)
	{
		const VkViewport viewport{
			.x        = x,
			.y        = y,
			.width    = width,
			.height   = height,
			.minDepth = minDepth,
			.maxDepth = maxDepth
		};
		vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
	}

	void VulkanCommandContext::SetScissor(const uint32_t x, const uint32_t y,
	                                      const uint32_t width, const uint32_t height)
	{
		const VkRect2D scissor{
			.offset = { static_cast<int32_t>(x), static_cast<int32_t>(y) },
			.extent = { width, height }
		};
		vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
	}

	void VulkanCommandContext::BindPipeline(const Ref<Pipeline>& pipeline)
	{
		SS_CORE_ASSERT(pipeline, "BindPipeline called with null pipeline");

		// Graphics only for now
		m_CurrentGraphicsPipeline = std::static_pointer_cast<VulkanGraphicsPipeline>(pipeline);

		vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CurrentGraphicsPipeline->GetHandle());

		m_CurrentPipelineLayout = m_CurrentGraphicsPipeline->GetPipelineLayout();
		m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}

	void VulkanCommandContext::BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, const uint32_t setIndex)
	{
		SS_CORE_ASSERT(descriptorSet, "BindDescriptorSet called with null descriptor set");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "BindDescriptorSet called before BindPipeline (need pipeline layout)");

		const auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
		const VkDescriptorSet setHandle = vkSet->GetHandle();

		vkCmdBindDescriptorSets(
			m_CommandBuffer,
			m_CurrentBindPoint,
			m_CurrentPipelineLayout,
			setIndex,
			1,
			&setHandle,
			0,
			nullptr);
	}

	void VulkanCommandContext::BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet,
	                                             const uint32_t setIndex,
	                                             const uint32_t* dynamicOffsets,
	                                             const uint32_t dynamicOffsetCount)
	{
		SS_CORE_ASSERT(descriptorSet, "BindDescriptorSet(dynamic) called with null descriptor set");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "BindDescriptorSet(dynamic) called before BindPipeline (need pipeline layout)");
		SS_CORE_ASSERT((dynamicOffsetCount == 0) || (dynamicOffsets != nullptr),
		               "BindDescriptorSet(dynamic): dynamicOffsets must be non-null when count > 0");

		const auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
		const VkDescriptorSet setHandle = vkSet->GetHandle();

		vkCmdBindDescriptorSets(
			m_CommandBuffer,
			m_CurrentBindPoint,
			m_CurrentPipelineLayout,
			setIndex,
			1,
			&setHandle,
			dynamicOffsetCount,
			dynamicOffsets);
	}

	void VulkanCommandContext::PushConstants(const void* data, const uint32_t size, const uint32_t offset)
	{
		SS_CORE_ASSERT(data, "PushConstants called with null data");
		SS_CORE_ASSERT(size > 0, "PushConstants size must be > 0");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "PushConstants called before BindPipeline (need pipeline layout)");
		SS_CORE_ASSERT(m_CurrentGraphicsPipeline, "PushConstants requires a bound VulkanGraphicsPipeline");

		const VkShaderStageFlags stages = m_CurrentGraphicsPipeline->GetVkPushConstantStagesFor(offset, size);
		SS_CORE_ASSERT(stages != 0,
		               "PushConstants range not declared in PipelineDesc::PushConstants (offset/size mismatch)");

		vkCmdPushConstants(m_CommandBuffer, m_CurrentPipelineLayout, stages, offset, size, data);
	}

	void VulkanCommandContext::ResetState()
	{
		m_IsRendering = false;
		m_CurrentPipelineLayout = VK_NULL_HANDLE;
		m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		m_CurrentGraphicsPipeline.reset();
	}

	void VulkanCommandContext::BindVertexBuffer(const Ref<Buffer>& vertexBuffer,
	                                            const uint32_t binding,
	                                            const uint64_t offset)
	{
		const auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(vertexBuffer);
		const VkBuffer buf = vkBuffer->GetHandle();
		const VkDeviceSize offs = offset;

		vkCmdBindVertexBuffers(m_CommandBuffer, binding, 1, &buf, &offs);
	}

	void VulkanCommandContext::Draw(const uint32_t vertexCount, const uint32_t instanceCount,
	                                const uint32_t firstVertex)
	{
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, 0);
	}

	void VulkanCommandContext::DrawIndexed(const Ref<Buffer>& indexBuffer,
	                                       const uint32_t indexCount,
	                                       const uint32_t instanceCount,
	                                       const uint32_t firstIndex,
	                                       const int32_t vertexOffset)
	{
		// Assume indexBuffer wraps a VulkanBuffer with index data.
		const auto vkIndexBuffer = std::static_pointer_cast<VulkanBuffer>(indexBuffer);
		const VkBuffer buf = vkIndexBuffer->GetHandle();

		// You may want to store index type in your Buffer or Pipeline.
		const VkIndexType indexType = VK_INDEX_TYPE_UINT32;

		vkCmdBindIndexBuffer(m_CommandBuffer, buf, 0, indexType);
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, 0);
	}

	void VulkanCommandContext::Dispatch(const uint32_t groupX,
	                                    const uint32_t groupY,
	                                    const uint32_t groupZ)
	{
		vkCmdDispatch(m_CommandBuffer, groupX, groupY, groupZ);
	}
}
