#pragma once

#include <cstdint>
#include <unordered_map>

namespace Snowstorm
{
	// Per-key cache whose entries are valid for one generation of the key's layout. A generation change
	// (shader hot-reload altered the binding interface) hands `onEvict` the stale value and default-constructs
	// a fresh one, so objects allocated against the old layout are never reused.
	template <typename Key, typename Value>
	class GenerationKeyedCache
	{
	public:
		template <typename OnEvict>
		Value& Acquire(const Key& key, const uint64_t generation, OnEvict&& onEvict)
		{
			Entry& entry = m_Entries[key];
			if (entry.Generation != generation)
			{
				onEvict(entry.Data);
				entry.Data = Value{};
				entry.Generation = generation;
			}
			return entry.Data;
		}

		Value& Acquire(const Key& key, const uint64_t generation)
		{
			return Acquire(key, generation, [](Value&) {});
		}

	private:
		struct Entry
		{
			uint64_t Generation = 0;
			Value Data{};
		};

		std::unordered_map<Key, Entry> m_Entries;
	};
}
