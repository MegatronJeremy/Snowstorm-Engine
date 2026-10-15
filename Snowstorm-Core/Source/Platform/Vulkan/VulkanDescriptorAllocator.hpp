#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/DescriptorPoolPolicy.hpp"
#include "Snowstorm/Render/DescriptorSetLayout.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace Snowstorm
{
	// Shared VkDescriptorPools for non-bindless sets, keyed by layout shape (see DescriptorPoolPolicy.hpp).
	// Sets are freed individually (FREE_DESCRIPTOR_SET_BIT) after a retire delay, so a set dropped while a
	// submitted frame still binds it is never recycled early. Pools live until Shutdown. Thread-safe.
	class VulkanDescriptorAllocator
	{
	public:
		struct Allocation
		{
			VkDescriptorSet Set = VK_NULL_HANDLE;
			uint32_t Key = DescriptorPoolBook::Invalid;
			uint32_t Pool = DescriptorPoolBook::Invalid;
		};

		static VulkanDescriptorAllocator& Get();

		// retireDelay = frames in flight + 1.
		void Init(VkDevice device, uint32_t retireDelay);
		void Shutdown();

		[[nodiscard]] Allocation Allocate(const DescriptorSetLayoutDesc& desc, VkDescriptorSetLayout layout);

		// Safe after Shutdown (no-op): sets outliving the device teardown are already gone with their pools.
		void Free(const Allocation& allocation);

		// Once per frame, after the in-flight fence wait.
		void BeginFrame();

		[[nodiscard]] uint32_t PoolCount() const;
		[[nodiscard]] uint32_t LiveSets() const;

	private:
		struct KeyEntry
		{
			DescriptorPoolKey Key;
			DescriptorPoolBook Book;
			std::vector<VkDescriptorPool> Pools;
		};

		uint32_t FindOrAddKey(const DescriptorPoolKey& key);
		uint32_t AddPool(uint32_t keyIndex);
		void FreeNow(const Allocation& a);

		mutable std::mutex m_Mutex;
		VkDevice m_Device = VK_NULL_HANDLE;
		std::vector<KeyEntry> m_Keys;
		std::unique_ptr<DescriptorRetireQueue<Allocation>> m_Retired;
		std::vector<Allocation> m_Ready;
		uint32_t m_RetireDelay = 3;
	};
}
