#include "DescriptorSetLayout.hpp"

#include "RendererAPI.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanDescriptorSetLayout.hpp"

namespace Snowstorm
{
	Ref<DescriptorSetLayout> DescriptorSetLayout::Create(const DescriptorSetLayoutDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL descriptor set layouts are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanDescriptorSetLayout>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 descriptor set layouts are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<DescriptorSetLayout> DescriptorSetLayout::CreateFromExternal(void* internalHandle)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL descriptor set layouts are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanDescriptorSetLayout>(internalHandle);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 descriptor set layouts are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
