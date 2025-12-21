#include "Sampler.hpp"
#include "RendererAPI.hpp"

#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanSampler.hpp"

namespace Snowstorm
{
	Ref<Sampler> Sampler::Create(const SamplerDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL samplers are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanSampler>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 samplers are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
