#include "Pipeline.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "RendererAPI.hpp"

#include "Platform/Vulkan/VulkanComputePipeline.hpp"
#include "Platform/Vulkan/VulkanGraphicsPipeline.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		// Weak registry of every live pipeline, so the shader-reload sweep can find them without any owner
		// having to publish its pipelines. Weak so the registry never keeps a pipeline alive past its owner;
		// dead entries are pruned during ForEachLive. The mutex guards only the vector: Create/Register may be
		// called from any thread, while Reload (vkDeviceWaitIdle + handle swap) must still run at a frame
		// boundary on the render thread.
		struct LiveState
		{
			std::mutex Mutex;
			std::vector<std::weak_ptr<Pipeline>> Entries;
		};

		LiveState& Live()
		{
			static LiveState s_state;
			return s_state;
		}
	}

	void Pipeline::Register(const Ref<Pipeline>& pipeline)
	{
		if (pipeline)
		{
			LiveState& live = Live();
			std::lock_guard lock(live.Mutex);
			live.Entries.push_back(pipeline);
		}
	}

	void Pipeline::ForEachLive(const std::function<void(const Ref<Pipeline>&)>& fn)
	{
		// Snapshot under the lock and call fn outside it: fn may create pipelines (re-entering Register).
		std::vector<Ref<Pipeline>> snapshot;
		{
			LiveState& live = Live();
			std::lock_guard lock(live.Mutex);
			snapshot.reserve(live.Entries.size());
			for (size_t i = 0; i < live.Entries.size();)
			{
				if (Ref<Pipeline> pipeline = live.Entries[i].lock())
				{
					snapshot.push_back(std::move(pipeline));
					++i;
				}
				else
				{
					live.Entries[i] = live.Entries.back();
					live.Entries.pop_back();
				}
			}
		}
		for (const Ref<Pipeline>& pipeline : snapshot)
		{
			fn(pipeline);
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