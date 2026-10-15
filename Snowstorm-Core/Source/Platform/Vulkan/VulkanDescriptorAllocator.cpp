#include "VulkanDescriptorAllocator.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		// Pool type slots follow the VkDescriptorType values used by non-bindless layouts (0..10).
		static_assert(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT < DescriptorPoolKey::kTypeSlots);
	}

	VulkanDescriptorAllocator& VulkanDescriptorAllocator::Get()
	{
		static VulkanDescriptorAllocator instance;
		return instance;
	}

	void VulkanDescriptorAllocator::Init(const VkDevice device, const uint32_t retireDelay)
	{
		std::scoped_lock lock(m_Mutex);
		m_Device = device;
		m_RetireDelay = retireDelay;
		m_Retired = std::make_unique<DescriptorRetireQueue<Allocation>>(retireDelay);
	}

	void VulkanDescriptorAllocator::Shutdown()
	{
		std::scoped_lock lock(m_Mutex);
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}

		// Destroying a pool frees its sets; the device is idle by contract.
		for (KeyEntry& k : m_Keys)
		{
			for (const VkDescriptorPool pool : k.Pools)
			{
				vkDestroyDescriptorPool(m_Device, pool, nullptr);
			}
		}
		m_Keys.clear();
		m_Retired.reset();
		m_Ready.clear();
		m_Device = VK_NULL_HANDLE;
	}

	uint32_t VulkanDescriptorAllocator::FindOrAddKey(const DescriptorPoolKey& key)
	{
		for (uint32_t i = 0; i < m_Keys.size(); ++i)
		{
			if (m_Keys[i].Key == key)
			{
				return i;
			}
		}
		m_Keys.push_back({key, {}, {}});
		return static_cast<uint32_t>(m_Keys.size() - 1);
	}

	uint32_t VulkanDescriptorAllocator::AddPool(const uint32_t keyIndex)
	{
		KeyEntry& k = m_Keys[keyIndex];
		const uint32_t ordinal = k.Book.AddPool();
		const uint32_t maxSets = k.Book.MaxSets(ordinal);

		std::vector<VkDescriptorPoolSize> sizes;
		for (uint32_t slot = 0; slot < DescriptorPoolKey::kTypeSlots; ++slot)
		{
			if (k.Key.TypeCounts[slot] != 0)
			{
				sizes.push_back({static_cast<VkDescriptorType>(slot), k.Key.TypeCounts[slot] * maxSets});
			}
		}

		VkDescriptorPoolCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		ci.maxSets = maxSets;
		ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
		ci.pPoolSizes = sizes.data();

		VkDescriptorPool pool = VK_NULL_HANDLE;
		const VkResult res = vkCreateDescriptorPool(m_Device, &ci, nullptr, &pool);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create shared VkDescriptorPool");
		k.Pools.push_back(pool);
		return ordinal;
	}

	VulkanDescriptorAllocator::Allocation VulkanDescriptorAllocator::Allocate(const DescriptorSetLayoutDesc& desc,
	                                                                          const VkDescriptorSetLayout layout)
	{
		DescriptorPoolKey key;
		for (const auto& b : desc.Bindings)
		{
			SS_CORE_ASSERT(!b.IsBindless, "Bindless layouts do not use the shared descriptor allocator");
			const VkDescriptorType vkType = ToVkDescriptorType(b.Type);
			SS_CORE_ASSERT(vkType != VK_DESCRIPTOR_TYPE_MAX_ENUM, "Unsupported DescriptorType");
			key.Add(static_cast<uint32_t>(vkType), b.Count);
		}

		std::scoped_lock lock(m_Mutex);
		SS_CORE_ASSERT(m_Device != VK_NULL_HANDLE, "VulkanDescriptorAllocator used before Init");

		const uint32_t keyIndex = FindOrAddKey(key);

		// One retry on a fresh pool: the driver may refuse a pool the book still counts as having room
		// (OUT_OF_POOL_MEMORY, FRAGMENTED_POOL).
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			KeyEntry& k = m_Keys[keyIndex];
			uint32_t pool = k.Book.Acquire();
			if (pool == DescriptorPoolBook::Invalid)
			{
				pool = AddPool(keyIndex);
				pool = k.Book.Acquire();
			}

			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = k.Pools[pool];
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &layout;

			VkDescriptorSet set = VK_NULL_HANDLE;
			const VkResult res = vkAllocateDescriptorSets(m_Device, &ai, &set);
			if (res == VK_SUCCESS)
			{
				return {set, keyIndex, pool};
			}

			SS_CORE_ASSERT(res == VK_ERROR_OUT_OF_POOL_MEMORY || res == VK_ERROR_FRAGMENTED_POOL,
			               "Failed to allocate VkDescriptorSet");
			k.Book.MarkExhausted(pool);
		}

		SS_CORE_ASSERT(false, "VkDescriptorSet allocation failed on a fresh pool");
		return {};
	}

	void VulkanDescriptorAllocator::Free(const Allocation& allocation)
	{
		if (allocation.Set == VK_NULL_HANDLE)
		{
			return;
		}

		std::scoped_lock lock(m_Mutex);
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}
		m_Retired->Retire(allocation);
	}

	void VulkanDescriptorAllocator::FreeNow(const Allocation& a)
	{
		KeyEntry& k = m_Keys[a.Key];
		vkFreeDescriptorSets(m_Device, k.Pools[a.Pool], 1, &a.Set);
		k.Book.Release(a.Pool);
	}

	void VulkanDescriptorAllocator::BeginFrame()
	{
		std::scoped_lock lock(m_Mutex);
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}

		m_Ready.clear();
		m_Retired->Advance(m_Ready);
		for (const Allocation& a : m_Ready)
		{
			FreeNow(a);
		}
	}

	uint32_t VulkanDescriptorAllocator::PoolCount() const
	{
		std::scoped_lock lock(m_Mutex);
		uint32_t n = 0;
		for (const KeyEntry& k : m_Keys)
		{
			n += k.Book.PoolCount();
		}
		return n;
	}

	uint32_t VulkanDescriptorAllocator::LiveSets() const
	{
		std::scoped_lock lock(m_Mutex);
		uint32_t n = 0;
		for (const KeyEntry& k : m_Keys)
		{
			n += k.Book.TotalLive();
		}
		return n;
	}
}
