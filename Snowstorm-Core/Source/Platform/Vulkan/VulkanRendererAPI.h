#pragma once

#include <vulkan/vulkan_core.h>

#include "VulkanCommandBuffers.h"
#include "Snowstorm/Render/RendererAPI.h"

namespace Snowstorm
{
	class VulkanRendererAPI final : public RendererAPI
	{
	public:
		void Init() override;

		void SetDepthFunction(DepthFunction func) override;
		void SetDepthMask(bool enable) override;

		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		void SetClearColor(const glm::vec4& color) override;
		void Clear() override;

		void DrawArrays(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) override;
		void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount) override;
		void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) override;

	private:
		Scope<VulkanCommandBuffers> m_VulkanCommandBuffer;
	};
}
