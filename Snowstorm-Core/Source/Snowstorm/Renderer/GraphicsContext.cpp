#include "pch.h"
#include "Snowstorm/Renderer/GraphicsContext.h"

#include "Snowstorm/Renderer/Renderer.h"
// #include "Platform/Vulkan/VulkanContext.h"

namespace Snowstorm
{
	Scope<GraphicsContext> GraphicsContext::Create(void* window)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None: SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;
		// case RendererAPI::API::Vulkan: return CreateScope<VulkanContext>(static_cast<GLFWwindow*>(window));
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
