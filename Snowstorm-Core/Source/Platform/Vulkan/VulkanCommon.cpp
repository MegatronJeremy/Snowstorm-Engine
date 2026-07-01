#include "VulkanCommon.hpp"
#include "VulkanContext.hpp"

namespace Snowstorm
{
	VulkanContext& GetVulkanContext()
	{
		return VulkanContext::Get();
	}

	VkInstance GetVulkanInstance()
	{
		return VulkanContext::Get().GetInstance();
	}

	VkDevice GetVulkanDevice()
	{
		return VulkanContext::Get().GetDevice();
	}

	VkPhysicalDevice GetVulkanPhysicalDevice()
	{
		return VulkanContext::Get().GetPhysicalDevice();
	}

	VkQueue GetGraphicsQueue()
	{
		return VulkanContext::Get().GetGraphicsQueue();
	}

	uint32_t GetGraphicsQueueFamilyIndex()
	{
		return VulkanContext::Get().GetGraphicsQueueFamilyIndex();
	}

	VmaAllocator GetAllocator()
	{
		return VulkanContext::Get().GetAllocator();
	}

	VkCommandPool GetGraphicsCommandPool()
	{
		return VulkanContext::Get().GetGraphicsCommandPool();
	}

	void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record)
	{
		VulkanContext& ctx = VulkanContext::Get();
		const VkDevice device = ctx.GetDevice();
		const VkQueue queue = ctx.GetGraphicsQueue();
		const VkCommandPool pool = ctx.GetGraphicsCommandPool();

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &cmd);
		assert(result == VK_SUCCESS && "Failed to allocate immediate command buffer");

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		result = vkBeginCommandBuffer(cmd, &beginInfo);
		assert(result == VK_SUCCESS && "Failed to begin immediate command buffer");

		// Let the caller record copy / transition commands
		record(cmd);

		result = vkEndCommandBuffer(cmd);
		assert(result == VK_SUCCESS && "Failed to end immediate command buffer");

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		VkFence fence = VK_NULL_HANDLE;
		result = vkCreateFence(device, &fenceInfo, nullptr, &fence);
		assert(result == VK_SUCCESS && "Failed to create fence for immediate submit");

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		result = vkQueueSubmit(queue, 1, &submitInfo, fence);
		assert(result == VK_SUCCESS && "Failed to submit immediate command buffer");

		// Wait only for this submission, not the whole queue.
		result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		assert(result == VK_SUCCESS && "Failed to wait for immediate fence");

		vkDestroyFence(device, fence, nullptr);
		vkFreeCommandBuffers(device, pool, 1, &cmd);
	}

	StageAccess LayoutStageAccess(const VkImageLayout layout)
	{
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
			return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT};
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0};
		case VK_IMAGE_LAYOUT_GENERAL:
			// Storage image for compute read/write (UAV): both access bits so a GENERAL image can be read
			// and written by the compute stage.
			return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT};
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			// ALL_TRANSFER (not COPY): the transfer-dst image is later read by BOTH copy and blit (mipmap
			// generation blits between levels). COPY_BIT alone wouldn't chain against the BLIT stage.
			return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
		default:
			return {VK_PIPELINE_STAGE_2_NONE, 0};
		}
	}

	void CmdTransitionImage(const VkCommandBuffer cmd,
	                        const VkImage image,
	                        const VkImageAspectFlags aspect,
	                        const VkImageLayout oldLayout,
	                        const VkImageLayout newLayout,
	                        const uint32_t mipLevels,
	                        const uint32_t layers)
	{
		VkImageMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

		// Tight src/dst scopes derived from the layouts (see LayoutStageAccess). UNDEFINED old layout ->
		// NONE/0 src: no prior work to wait on and the old contents are discarded.
		const StageAccess src = LayoutStageAccess(oldLayout);
		const StageAccess dst = LayoutStageAccess(newLayout);

		barrier.srcStageMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_NONE : src.Stage;
		barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : src.Access;
		barrier.dstStageMask = dst.Stage;
		barrier.dstAccessMask = dst.Access;

		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.image = image;

		barrier.subresourceRange.aspectMask = aspect;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = layers;

		VkDependencyInfo dep{};
		dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(cmd, &dep);
	}

	void SetVulkanObjectName(const VkDevice device, const uint64_t objectHandle, const VkObjectType objectType, const char* name)
	{
		// Requires VK_EXT_debug_utils. The entry point is only loaded when validation/debug-utils
		// is active; skip (don't crash) when it isn't, or when there's nothing to name.
		if (vkSetDebugUtilsObjectNameEXT == nullptr || objectHandle == 0 || name == nullptr)
		{
			return;
		}

		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = objectType;
		nameInfo.objectHandle = objectHandle;
		nameInfo.pObjectName = name;

		vkSetDebugUtilsObjectNameEXT(device, &nameInfo);
	}
}
