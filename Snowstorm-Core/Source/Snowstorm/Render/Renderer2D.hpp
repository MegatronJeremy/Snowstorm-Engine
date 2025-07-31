#pragma once

#include "Camera.hpp"
#include "RendererAPI.hpp"
#include "Texture.hpp"

namespace Snowstorm
{
	// Batch renderer for 2D quads
	class Renderer2D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void EndScene();

		static void Flush();

		// Primitives
		static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
		static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor);

		// Stats
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;

			[[nodiscard]] uint32_t GetTotalVertexCount() const { return QuadCount * 4; }
			[[nodiscard]] uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
		};

		static void ResetStats();
		static Statistics GetStats();

		// API
		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

	private:
		static void FlushAndReset();
	};
}
