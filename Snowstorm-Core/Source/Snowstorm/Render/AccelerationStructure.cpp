#include "AccelerationStructure.hpp"

#include "RendererAPI.hpp"

#include "Platform/Vulkan/VulkanBlas.hpp"
#include "Platform/Vulkan/VulkanTlas.hpp"

namespace Snowstorm
{
	Ref<BLAS> BLAS::Create(const Ref<Buffer>& vertexBuffer, const uint32_t vertexCount, const uint32_t vertexStride,
	                       const uint32_t positionOffset, const Ref<Buffer>& indexBuffer, const uint32_t indexCount,
	                       const std::string& debugName)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanBlas>(vertexBuffer, vertexCount, vertexStride, positionOffset, indexBuffer,
			                             indexCount, debugName);

		case RendererAPI::API::None:
		case RendererAPI::API::OpenGL:
		case RendererAPI::API::DX12:
		default:
			SS_CORE_ASSERT(false, "BLAS::Create: only the Vulkan backend supports acceleration structures");
			return nullptr;
		}
	}

	Ref<TLAS> TLAS::Create(const std::string& debugName)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanTlas>(debugName);

		case RendererAPI::API::None:
		case RendererAPI::API::OpenGL:
		case RendererAPI::API::DX12:
		default:
			SS_CORE_ASSERT(false, "TLAS::Create: only the Vulkan backend supports acceleration structures");
			return nullptr;
		}
	}
}
