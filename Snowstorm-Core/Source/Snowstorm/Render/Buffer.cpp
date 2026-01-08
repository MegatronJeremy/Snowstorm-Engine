#include "pch.h"
#include "Buffer.hpp"

#include "RendererAPI.hpp"

#include "Platform/Vulkan/VulkanBuffer.hpp"

namespace Snowstorm
{
	Ref<Buffer> Buffer::Create(size_t size, BufferUsage usage, const void* data, bool hostVisible, const std::string& debugName)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "RendererAPI::OpenGL is currently not supported!");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanBuffer>(size, usage, data, hostVisible, debugName);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "RendererAPI::DX12 is currently not supported!");
			return nullptr;

		default:
			return nullptr;
		}
	}
}
