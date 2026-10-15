#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Backend-free policy for a pooled descriptor-set allocator: which pool a layout shares, how big the
	// next pool is, and when a freed set may be recycled. Not thread-safe: the owner locks.

	// Descriptor-type counts of one set, indexed by a backend-defined type slot. Layouts with equal
	// counts share pools, so the key is the shape and not the layout identity (a hot-reloaded layout with
	// the same bindings keeps its pools).
	struct DescriptorPoolKey
	{
		static constexpr uint32_t kTypeSlots = 16;
		std::array<uint32_t, kTypeSlots> TypeCounts{};

		bool operator==(const DescriptorPoolKey& other) const { return TypeCounts == other.TypeCounts; }

		void Add(const uint32_t typeSlot, const uint32_t count)
		{
			if (typeSlot < kTypeSlots)
			{
				TypeCounts[typeSlot] += count;
			}
		}
	};

	inline constexpr uint32_t kDescriptorPoolFirstSets = 64;
	inline constexpr uint32_t kDescriptorPoolMaxSets = 1024;

	// maxSets for the Nth pool (0-based) of one key: 64, 128, ... capped at 1024. Doubling bounds the
	// pool count at O(log n) per key while a pool never reserves more than ~2x what was needed.
	[[nodiscard]] constexpr uint32_t DescriptorPoolSetCount(const uint32_t poolOrdinal)
	{
		uint32_t sets = kDescriptorPoolFirstSets;
		for (uint32_t i = 0; i < poolOrdinal && sets < kDescriptorPoolMaxSets; ++i)
		{
			sets *= 2;
		}
		return sets;
	}

	// Slot accounting for the pools of one key. The backend creates the VkDescriptorPool objects; this
	// only decides which one a new set comes from.
	class DescriptorPoolBook
	{
	public:
		static constexpr uint32_t Invalid = ~0u;

		// Registers a pool created with DescriptorPoolSetCount(PoolCount()); returns its index.
		uint32_t AddPool()
		{
			const uint32_t ordinal = static_cast<uint32_t>(m_Pools.size());
			m_Pools.push_back({DescriptorPoolSetCount(ordinal), 0, false});
			return ordinal;
		}

		// First pool with room, or Invalid: the caller adds a pool (and also does so on the driver's
		// OUT_OF_POOL_MEMORY / FRAGMENTED_POOL, then calls MarkExhausted on the pool that failed).
		uint32_t Acquire()
		{
			for (uint32_t i = 0; i < m_Pools.size(); ++i)
			{
				Pool& p = m_Pools[i];
				if (!p.Exhausted && p.Live < p.MaxSets)
				{
					++p.Live;
					return i;
				}
			}
			return Invalid;
		}

		// A pool the driver refused: skipped until a set is released back to it.
		void MarkExhausted(const uint32_t pool)
		{
			if (pool < m_Pools.size())
			{
				--m_Pools[pool].Live;
				m_Pools[pool].Exhausted = true;
			}
		}

		void Release(const uint32_t pool)
		{
			if (pool < m_Pools.size() && m_Pools[pool].Live > 0)
			{
				--m_Pools[pool].Live;
				m_Pools[pool].Exhausted = false;
			}
		}

		[[nodiscard]] uint32_t PoolCount() const { return static_cast<uint32_t>(m_Pools.size()); }
		[[nodiscard]] uint32_t Live(const uint32_t pool) const { return m_Pools[pool].Live; }
		[[nodiscard]] uint32_t MaxSets(const uint32_t pool) const { return m_Pools[pool].MaxSets; }

		[[nodiscard]] uint32_t TotalLive() const
		{
			uint32_t n = 0;
			for (const Pool& p : m_Pools)
			{
				n += p.Live;
			}
			return n;
		}

	private:
		struct Pool
		{
			uint32_t MaxSets;
			uint32_t Live;
			bool Exhausted;
		};

		std::vector<Pool> m_Pools;
	};

	// Deferred free list. A set destroyed while a submitted frame still binds it must not be freed (or
	// reused) until that frame retired; Advance() is called once per frame after the fence wait, and a
	// delay of (frames in flight + 1) is sufficient. Same contract as BindlessSlotAllocator.
	template <typename T>
	class DescriptorRetireQueue
	{
	public:
		explicit DescriptorRetireQueue(const uint32_t retireDelay) : m_RetireDelay(retireDelay) {}

		void Retire(const T& item) { m_Pending.push_back({item, m_Epoch + m_RetireDelay}); }

		// Appends every entry whose delay elapsed to `ready`, in retire order.
		void Advance(std::vector<T>& ready)
		{
			++m_Epoch;
			size_t kept = 0;
			for (const Entry& e : m_Pending)
			{
				if (e.ReadyEpoch <= m_Epoch)
				{
					ready.push_back(e.Item);
				}
				else
				{
					m_Pending[kept++] = e;
				}
			}
			m_Pending.resize(kept);
		}

		// Teardown: the device is idle, so everything is ready.
		void Drain(std::vector<T>& ready)
		{
			for (const Entry& e : m_Pending)
			{
				ready.push_back(e.Item);
			}
			m_Pending.clear();
		}

		[[nodiscard]] size_t Pending() const { return m_Pending.size(); }

	private:
		struct Entry
		{
			T Item;
			uint64_t ReadyEpoch;
		};

		uint32_t m_RetireDelay;
		uint64_t m_Epoch = 0;
		std::vector<Entry> m_Pending;
	};
}
