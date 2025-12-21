#include "RenderTarget.hpp"

#include "RendererAPI.hpp"
#include "Platform/Vulkan/VulkanRenderTarget.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> RenderTarget::Create(const RenderTargetDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "RendererAPI::API::OpenGL is currently not supported!");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanRenderTarget>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "RendererAPI::API::DX12 is currently not supported!");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
