#pragma once

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Index allocator for one bindless descriptor array. Not thread-safe: the owner locks.
	//
	// A released slot is quarantined for `retireDelay` calls to Advance() before it can be handed out again.
	// The GPU may still be sampling the old descriptor in a frame in flight, and rewriting it for a new
	// owner earlier would make that frame read the wrong texture. Advance() is called once per frame, after
	// the fence wait, so a delay of (frames in flight + 1) is sufficient.
	class BindlessSlotAllocator
	{
	public:
		static constexpr uint32_t Invalid = ~0u;

		// Indices below `firstIndex` are never handed out (sentinel slots).
		BindlessSlotAllocator(const uint32_t capacity, const uint32_t firstIndex, const uint32_t retireDelay)
		    : m_Capacity(capacity), m_RetireDelay(retireDelay), m_NextFresh(firstIndex), m_FirstIndex(firstIndex)
		{
		}

		// Returns Invalid when the array is exhausted.
		uint32_t Allocate()
		{
			if (!m_Free.empty())
			{
				const uint32_t index = m_Free.back();
				m_Free.pop_back();
				return index;
			}
			if (m_NextFresh >= m_Capacity)
			{
				return Invalid;
			}
			return m_NextFresh++;
		}

		// Sentinel and out-of-range indices are ignored, so a view that never registered can call this.
		void Release(const uint32_t index)
		{
			if (index < m_FirstIndex || index >= m_NextFresh)
			{
				return;
			}
			m_Retiring.push_back({index, m_Epoch + m_RetireDelay});
		}

		void Advance()
		{
			++m_Epoch;
			size_t kept = 0;
			for (const Pending& p : m_Retiring)
			{
				if (p.ReadyEpoch <= m_Epoch)
				{
					m_Free.push_back(p.Index);
				}
				else
				{
					m_Retiring[kept++] = p;
				}
			}
			m_Retiring.resize(kept);
		}

		// Slots handed out and not yet reusable (live plus quarantined).
		[[nodiscard]] uint32_t InUse() const
		{
			return (m_NextFresh - m_FirstIndex) - static_cast<uint32_t>(m_Free.size());
		}

		// True when `index` was handed out at some point (live or retiring); used to validate rewrites.
		[[nodiscard]] bool WasAllocated(const uint32_t index) const { return index < m_NextFresh; }

	private:
		struct Pending
		{
			uint32_t Index;
			uint64_t ReadyEpoch;
		};

		uint32_t m_Capacity;
		uint32_t m_RetireDelay;
		uint32_t m_NextFresh;
		uint32_t m_FirstIndex;
		uint64_t m_Epoch = 0;
		std::vector<uint32_t> m_Free;
		std::vector<Pending> m_Retiring;
	};
}
