#include "Pipeline.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "RendererAPI.hpp"

#include "Platform/Vulkan/VulkanComputePipeline.hpp"
#include "Platform/Vulkan/VulkanGraphicsPipeline.hpp"

#include <memory>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		// Weak registry of every live pipeline, so the shader-reload sweep can find them without any owner
		// having to publish its pipelines. Weak so the registry never keeps a pipeline alive past its owner;
		// dead entries are pruned lazily during ForEachLive. Single-threaded (all pipeline create/reload
		// happens on the render thread), so no lock.
		std::vector<std::weak_ptr<Pipeline>>& LiveRegistry()
		{
			static std::vector<std::weak_ptr<Pipeline>> s_registry;
			return s_registry;
		}
	}

	void Pipeline::Register(const Ref<Pipeline>& pipeline)
	{
		if (pipeline)
		{
			LiveRegistry().push_back(pipeline);
		}
	}

	void Pipeline::ForEachLive(const std::function<void(const Ref<Pipeline>&)>& fn)
	{
		auto& registry = LiveRegistry();
		for (size_t i = 0; i < registry.size();)
		{
			if (Ref<Pipeline> live = registry[i].lock())
			{
				fn(live);
				++i;
			}
			else
			{
				// Prune a dead entry by swapping the last one into its slot (order doesn't matter here).
				registry[i] = registry.back();
				registry.pop_back();
			}
		}
	}

	Ref<Pipeline> Pipeline::Create(const PipelineDesc& desc)
	{
		Ref<Pipeline> pipeline;
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL pipelines are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			pipeline = desc.Type == PipelineType::Compute
			               ? Ref<Pipeline>(CreateRef<VulkanComputePipeline>(desc))
			               : Ref<Pipeline>(CreateRef<VulkanGraphicsPipeline>(desc));
			Register(pipeline);
			return pipeline;

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 pipelines are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}