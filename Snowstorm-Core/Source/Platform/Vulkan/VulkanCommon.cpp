#include "VulkanCommon.hpp"
#include "VulkanContext.hpp"

namespace Snowstorm::VulkanCommon
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
}
