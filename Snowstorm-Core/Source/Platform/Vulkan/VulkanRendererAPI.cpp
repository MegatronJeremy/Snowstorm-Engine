#include "pch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanInstance.h"
#include "VulkanSwapChain.h"
#include "VulkanCommandPool.h"
#include "VulkanContext.h"

namespace Snowstorm
{
	void VulkanRendererAPI::Init()
	{
		SS_PROFILE_FUNCTION();

		VkDevice device = VulkanInstance::GetInstance()->GetVulkanDevice()->GetVkDevice();
		VkCommandPool commandPool = *VulkanInstance::GetInstance()->GetVulkanCommandPool();

		m_VulkanCommandBuffer = CreateScope<VulkanCommandBuffers>(device, commandPool, 1);
	}

	void VulkanRendererAPI::SetDepthFunction(DepthFunction func)
	{
		// TODO implement this
	}

	void VulkanRendererAPI::SetDepthMask(const bool enable)
	{
		// TODO implement this
	}

	void VulkanRendererAPI::SetViewport(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height)
	{
		// this is a delayed submission of a render command
		// Set viewport
		VkViewport viewport{};
		viewport.x = static_cast<float>(x);
		viewport.y = static_cast<float>(y);
		viewport.width = static_cast<float>(width);
		viewport.height = static_cast<float>(height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VulkanSwapChain::SetViewport(viewport);
	}

	void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
	{
		const VkClearValue clearColor = {{color.r, color.g, color.b, color.a}};

		VulkanSwapChain::SetClearValue(clearColor);
	}

	void VulkanRendererAPI::Clear()
	{
		// TODO this should not actually exist - move it to swap buffers in OpenGl
	}

	void VulkanRendererAPI::DrawArrays(const Ref<VertexArray>& vertexArray, uint32_t vertexCount)
	{
		// TODO implement
	}

	void VulkanRendererAPI::DrawIndexed(const Ref<VertexArray>& vertexArray, const uint32_t indexCount)
	{
		const VulkanDrawCallCommand drawCallCommand{
			vertexArray,
			indexCount,
			VulkanContext::GetUniformBufferObject()
		};

		VulkanSwapChainQueue::GetInstance()->AddDrawCall(drawCallCommand);
	}

	void VulkanRendererAPI::DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, const uint32_t indexCount, const uint32_t instanceCount)
	{
		// TODO implement
	}
}
