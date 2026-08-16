#pragma once

#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"

namespace Snowstorm
{
	class RendererAPI : public NonCopyable
	{
	public:
		enum class API : uint8_t
		{
			None = 0,
			OpenGL = 1,
			Vulkan = 2,
			DX12 = 3,
		};

		virtual void Init(void* windowHandle) = 0;
		virtual void Shutdown() = 0;

		// Block until the GPU has finished all submitted work. Call before tearing down GPU
		// resources so they aren't destroyed while still referenced by in-flight command buffers.
		virtual void WaitIdle() = 0;

		// Returns false when a frame could not be started (swapchain not ready, e.g. minimized
		// window). The caller must skip rendering and not call EndFrame for this frame.
		virtual bool BeginFrame() = 0;
		virtual void EndFrame() = 0;

		// CPU time (ms) spent in the last BeginFrame blocking on the in-flight fence — i.e. waiting
		// for the GPU/vsync. This is "GPU/present wait", not CPU work; the editor overlay shows it
		// separately so a system's wall-clock time isn't misread as CPU cost when it's really a stall.
		virtual float GetLastGpuWaitMs() const = 0;

		// GPU execution time (ms) of the most recently completed frame, measured with timestamp queries
		// (write at command-buffer begin/end, read one frame later). Unlike GetLastGpuWaitMs (a CPU
		// stall on the present fence), this is real GPU work. Returns 0 if timestamps are unsupported.
		virtual float GetLastGpuFrameMs() const = 0;

		// VSync: true = locked to refresh (FIFO, no tearing); false = uncapped (MAILBOX/IMMEDIATE).
		// Switching recreates the swapchain.
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;

		// Index of the frame in flight (0..N-1) if using a multi-buffered setup
		virtual uint32_t GetCurrentFrameIndex() const = 0;

		// how many frames are in flight (N)
		virtual uint32_t GetFramesInFlight() const = 0;

		virtual PixelFormat GetSurfaceFormat() const = 0;

		virtual Ref<RenderTarget> GetSwapchainTarget() const = 0;

		// required alignment for dynamic uniform buffer offsets (bytes)
		virtual uint32_t GetMinUniformBufferOffsetAlignment() const = 0;

		// GPU device name (VkPhysicalDeviceProperties::deviceName), for per-machine perf/occupancy baselines.
		virtual std::string GetDeviceName() const = 0;
		// TODO turn this into GetCapabilities

		// True when the device supports (and enabled) inline ray tracing (VK_KHR_ray_query + AS). Gates the
		// whole RT feature path (#118); false => raster fallback. A device capability, hence on RendererAPI.
		virtual bool IsRayTracingSupported() const = 0;

		// True when the device supports (and enabled) fp16 shader math + 16-bit storage. Gates the neural conv's
		// fp16 permutation (# fp16 inference); false => fp32 fallback. A device capability, hence on RendererAPI.
		virtual bool IsFloat16Supported() const = 0;

		// Max MSAA sample count usable for BOTH color and depth framebuffer attachments (the intersection of
		// framebufferColorSampleCounts & framebufferDepthSampleCounts), as a count (1/2/4/8/...). render.msaa is
		// clamped to this. A device capability, hence on RendererAPI.
		virtual uint32_t GetMaxSampleCount() const = 0;

		//-- acquire a new command context for recording
		virtual Ref<CommandContext> GetGraphicsCommandContext() = 0;

		//-- ImGui Backend Abstraction
		virtual void InitImGuiBackend(void* windowHandle) = 0;
		virtual void ShutdownImGuiBackend() = 0;
		virtual void ImGuiNewFrame() = 0;
		virtual void RenderImGuiDrawData(CommandContext& context) = 0;

		static API GetAPI() { return s_API; }
		static void SetAPI(const API api) { s_API = api; }

	private:
		inline static API s_API = API::Vulkan;
	};
}
