#pragma once

#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
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

		virtual void BindVertexBuffer(const Ref<Buffer>& vertexBuffer, uint32_t binding = 0, uint64_t offset = 0) = 0;

		virtual void PushConstants(const void* data, uint32_t size, uint32_t offset = 0) = 0;

		// Draw commands
		virtual void Draw(uint32_t vertexCount,
		                  uint32_t instanceCount = 1,
		                  uint32_t firstVertex = 0) = 0;

		virtual void DrawIndexed(const Ref<Buffer>& indexBuffer,
		                         uint32_t indexCount,
		                         uint32_t instanceCount = 1,
		                         uint32_t firstIndex = 0,
		                         int32_t vertexOffset = 0) = 0;

		// Dispatch (compute support)
		virtual void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) = 0;

		// Reset the internal state between passes if the backend needs it
		virtual void ResetState() = 0;
	};
}
