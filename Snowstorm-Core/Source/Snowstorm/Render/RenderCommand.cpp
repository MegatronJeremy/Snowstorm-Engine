#include "pch.h"
#include "RenderCommand.hpp"

#include "Platform/OpenGL/OpenGLRendererAPI.h"
#include "Platform/Vulkan/VulkanRendererAPI.hpp"

namespace Snowstorm
{
	namespace
	{
		RendererAPI* CreateRendererAPI()
		{
			switch (RendererAPI::GetAPI())
			{
			case RendererAPI::API::None:
				SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
				return nullptr;
			case RendererAPI::API::OpenGL:
				return new OpenGLRendererAPI;
			case RendererAPI::API::Vulkan:
				return new VulkanRendererAPI;
			}

			SS_CORE_ASSERT(false, "Unknown RendererAPI!");
			return nullptr;
		}
	}

	RendererAPI* RenderCommand::s_RendererAPI = CreateRendererAPI();
}
