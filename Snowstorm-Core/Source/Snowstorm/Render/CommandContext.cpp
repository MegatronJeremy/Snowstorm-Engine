#include "CommandContext.hpp"

#include "RendererAPI.hpp"

namespace Snowstorm
{
	Ref<CommandContext> CommandContext::Create()
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;
		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "RendererAPI::DX12 is currently not supported!");
			return nullptr;
		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanCommandContext>();
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}
