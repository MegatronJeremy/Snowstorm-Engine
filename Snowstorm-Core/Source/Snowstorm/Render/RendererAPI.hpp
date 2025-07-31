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
			Vulkan = 1,
			DX12 = 2
		};

		virtual void Init() = 0;

		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;

		//-- acquire a new command context for recording
		virtual Ref<CommandContext> GetCommandContext() = 0;

		static API GetAPI() { return s_API; }

	private:
		inline static API s_API = API::Vulkan;
	};
}
