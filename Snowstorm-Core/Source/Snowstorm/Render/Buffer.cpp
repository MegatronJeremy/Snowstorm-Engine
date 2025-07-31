#include "pch.h"
#include "Buffer.hpp"

#include "Renderer2D.hpp"

#include "Platform/Vulkan/VulkanBuffer.hpp"

namespace Snowstorm
{
	Ref<Buffer> Buffer::Create(size_t size, BufferUsage usage, const void* data, bool hostVisible)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanBuffer>(size, usage, data, hostVisible);
		case RendererAPI::API::DX12:
			// return CreateRef<DX12Buffer>(...);
		default:
			return nullptr;
		}
	}
}
