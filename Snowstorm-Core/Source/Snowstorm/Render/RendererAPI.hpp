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

		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;

		// Index of the frame in flight (0..N-1) if using a multi-buffered setup
		virtual uint32_t GetCurrentFrameIndex() const = 0;

		// how many frames are in flight (N)
		virtual uint32_t GetFramesInFlight() const = 0;

		virtual PixelFormat GetSurfaceFormat() const = 0;

		virtual Ref<RenderTarget> GetSwapchainTarget() const = 0;

		// required alignment for dynamic uniform buffer offsets (bytes)
		virtual uint32_t GetMinUniformBufferOffsetAlignment() const = 0;
		// TODO turn this into GetCapabilities

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
