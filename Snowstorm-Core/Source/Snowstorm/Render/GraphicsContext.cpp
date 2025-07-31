#include "pch.h"

#include "GraphicsContext.hpp"

#include "Renderer2D.hpp"
#include "Platform/OpenGL/OpenGLContext.h"
#include "Platform/Vulkan/VulkanContext.hpp"

namespace Snowstorm
{
	Scope<GraphicsContext> GraphicsContext::Create(void* window)
	{
		switch (Renderer2D::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;
		case RendererAPI::API::OpenGL:
			return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
		case RendererAPI::API::Vulkan:
			return CreateScope<VulkanContext>(static_cast<GLFWwindow*>(window));
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
