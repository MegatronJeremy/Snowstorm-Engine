#include "DescriptorSet.hpp"

#include "RendererAPI.hpp"
#include "Platform/Vulkan/VulkanDescriptorSet.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Ref<DescriptorSet> DescriptorSet::Create(const Ref<DescriptorSetLayout>& layout, const DescriptorSetDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL descriptor sets are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanDescriptorSet>(layout, desc);

		case RendererAPI::API::DX12:
			// Implement a DX12DescriptorSet that derives from DescriptorSet and return it here.
			SS_CORE_ASSERT(false, "DX12 DescriptorSet backend not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
