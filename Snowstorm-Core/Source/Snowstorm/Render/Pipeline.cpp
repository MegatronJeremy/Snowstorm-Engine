#include "Pipeline.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "RendererAPI.hpp"

#include "Platform/Vulkan/VulkanGraphicsPipeline.hpp"

namespace Snowstorm
{
	Ref<Pipeline> Pipeline::Create(const PipelineDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL pipelines are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanGraphicsPipeline>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 pipelines are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}