// Snowstorm-Core/Source/Snowstorm/Render/Renderer.hpp
#pragma once

#include "UniformRingBuffer.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"

namespace Snowstorm
{
	class Renderer final : public NonCopyable
	{
	public:
		static void Init(void* windowHandle);
		static void Shutdown();

		// Block until the GPU is idle. Call before destroying GPU resources (e.g. on app teardown).
		static void WaitIdle();

		// Returns false when no frame could be started (swapchain not ready, e.g. minimized window);
		// the caller must then skip all rendering and NOT call EndFrame for this frame.
		static bool BeginFrame();
		static void EndFrame();

		// CPU ms spent in the last BeginFrame waiting on the GPU/vsync fence (a stall, not CPU work).
		static float GetLastGpuWaitMs();

		// Current frame info
		static uint32_t GetCurrentFrameIndex();
		static uint32_t GetFramesInFlight();

		static PixelFormat GetSurfaceFormat();

		static Ref<RenderTarget> GetSwapchainTarget();

		static UniformRingBuffer& GetFrameUniformRing();

		static uint32_t GetMinUniformBufferOffsetAlignment();

		static Ref<CommandContext> GetGraphicsCommandContext();

		static Ref<DescriptorSetLayout> GetUITextureLayout();
		static Ref<Sampler> GetUISampler();

		static void InitImGuiBackend(void* windowHandle);
		static void ShutdownImGuiBackend();
		static void ImGuiNewFrame();
		static void RenderImGuiDrawData(CommandContext& context);

		// True between InitImGuiBackend() and ShutdownImGuiBackend(). The editor brings the
		// ImGui backend up; a packaged runtime does not, so it can skip ImGui-only work.
		static bool IsImGuiBackendInitialized();

		// Access to backend (avoid using this unless you must)
		static RendererAPI& GetAPI();

	private:
		static Scope<RendererAPI> s_API;
		static bool s_ImGuiBackendInitialized;

		// TODO move this to RendererAPI
		static std::vector<UniformRingBuffer> s_FrameUniformRings;
	};
}