#pragma once

#include "RendererAPI.hpp"

namespace Snowstorm
{
	class RenderCommand
	{
	public:
		static void Init()
		{
			s_RendererAPI->Init();
		}

		static void SetDepthFunction(const DepthFunction func)
		{
			s_RendererAPI->SetDepthFunction(func);
		}

		static void SetDepthMask(const bool enable)
		{
			s_RendererAPI->SetDepthMask(enable);
		}

		static void SetViewport(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height)
		{
			s_RendererAPI->SetViewport(x, y, width, height);
		}

		static void SetClearColor(const glm::vec4& color)
		{
			s_RendererAPI->SetClearColor(color);
		}

		static void Clear()
		{
			s_RendererAPI->Clear();
		}

		static void DrawArrays(const Ref<VertexArray>& vertexArray, const uint32_t vertexCount)
		{
			s_RendererAPI->DrawArrays(vertexArray, vertexCount);
		}

		static void DrawIndexed(const Ref<VertexArray>& vertexArray, const uint32_t indexCount = 0)
		{
			s_RendererAPI->DrawIndexed(vertexArray, indexCount);
		}

		static void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, const uint32_t indexCount, const uint32_t instanceCount)
		{
			s_RendererAPI->DrawIndexedInstanced(vertexArray, indexCount, instanceCount);
		}

	private:
		static RendererAPI* s_RendererAPI;
	};
}
