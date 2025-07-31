#pragma once

#include "Snowstorm/Render/RendererAPI.hpp"

namespace Snowstorm
{
	class VulkanRendererAPI final : public RendererAPI
	{
	public:
		void Init() override;

		void BeginFrame() override;
		void EndFrame() override;

		Ref<CommandContext> GetCommandContext() override;
	};
}
