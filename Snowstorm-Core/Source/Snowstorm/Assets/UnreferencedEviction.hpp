#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Snowstorm
{
	// Holds evicted resources for `retireDelay` calls to Advance() before dropping them. The GPU may still
	// read a buffer in a frame in flight, so the last reference outlives the cache entry by (frames in
	// flight + 1) frames. Not thread-safe: main thread only.
	template <typename T>
	class RetireQueue
	{
	public:
		void Push(T value, const uint32_t retireDelay)
		{
			m_Items.push_back({std::move(value), m_Epoch + retireDelay});
		}

		void Advance()
		{
			++m_Epoch;
			size_t kept = 0;
			for (size_t i = 0; i < m_Items.size(); ++i)
			{
				if (m_Items[i].ReadyEpoch > m_Epoch)
				{
					if (kept != i)
					{
						m_Items[kept] = std::move(m_Items[i]);
					}
					++kept;
				}
			}
			m_Items.resize(kept);
		}

		[[nodiscard]] size_t Size() const { return m_Items.size(); }

	private:
		struct Item
		{
			T Value;
			uint64_t ReadyEpoch;
		};
		uint64_t m_Epoch = 0;
		std::vector<Item> m_Items;
	};

	// Removes every map entry whose shared_ptr has no owner besides the cache itself and the references
	// `extraRefs(value)` accounts for (another cache holding the same object), moving it into `retired`;
	// returns how many. Entries still held by a component, pipeline or in-flight job stay.
	// onEvict(key, value) runs per removed entry so a caller can drop side tables keyed alongside.
	template <typename Map, typename Retired, typename OnEvict, typename ExtraRefs>
	size_t EvictUnreferenced(Map& map, Retired& retired, const uint32_t retireDelay, OnEvict&& onEvict, ExtraRefs&& extraRefs)
	{
		size_t evicted = 0;
		for (auto it = map.begin(); it != map.end();)
		{
			if (it->second && static_cast<size_t>(it->second.use_count()) == 1 + extraRefs(it->second))
			{
				onEvict(it->first, it->second);
				retired.Push(std::move(it->second), retireDelay);
				it = map.erase(it);
				++evicted;
			}
			else
			{
				++it;
			}
		}
		return evicted;
	}
}
