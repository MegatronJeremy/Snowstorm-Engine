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

		// GPU execution time (ms) of the last completed frame (timestamp queries; 0 if unsupported).
		static float GetLastGpuFrameMs();

		// VSync on = locked to refresh (FIFO); off = uncapped (MAILBOX/IMMEDIATE). Recreates swapchain.
		static void SetVSync(bool enabled);
		static bool IsVSync();

		// Current frame info
		static uint32_t GetCurrentFrameIndex();
		static uint32_t GetFramesInFlight();

		static PixelFormat GetSurfaceFormat();

		static Ref<RenderTarget> GetSwapchainTarget();

		static UniformRingBuffer& GetFrameUniformRing();

		static uint32_t GetMinUniformBufferOffsetAlignment();

		static std::string GetDeviceName();

		// True when the device supports + enabled inline ray tracing (VK_KHR_ray_query + AS). Gates the RT
		// path (#118); false => raster fallback. Forwards to the backend capability query.
		static bool IsRayTracingSupported();

		// True when the device supports + enabled opacity micromaps (VK_EXT_opacity_micromap). Gates the OMM
		// bake/attach path; false => the any-hit alpha test alone handles cutout geometry.
		static bool IsOpacityMicromapSupported();

		// GPU candidates (candidate-index order = render.gpu index) + the chosen index. For the editor's GPU
		// picker; the selection is written to render.gpu (Persist) and applies on the next launch.
		static const std::vector<std::string>& GetGpuNames();
		static int GetSelectedGpuIndex();

		// True when the device supports + enabled fp16 shader math + 16-bit storage. Gates the neural conv's
		// fp16 permutation (# fp16 inference); false => fp32 fallback. Forwards to the backend capability query.
		static bool IsFloat16Supported();

		// Max MSAA sample count usable for both color+depth attachments (1/2/4/8). render.msaa is clamped to it.
		static uint32_t GetMaxSampleCount();

		// The currently open recording segment, which changes across a fork, so do not cache it. Record
		// through the context the render graph hands each pass.
		static Ref<CommandContext> GetGraphicsCommandContext();

		// Driven by RenderGraph::Execute. Passes declare Pass::Queue and never call these directly.
		static bool IsAsyncComputeAvailable();
		static const std::vector<GpuScope>& GetCollectedGpuScopes();
		static Ref<CommandContext> ForkAsyncCompute();
		static void JoinAsyncCompute();

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