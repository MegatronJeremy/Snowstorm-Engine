#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Snowstorm
{
	// Runs destroy callbacks once the GPU can no longer be reading what they free. Retire() stamps an entry with
	// the current frame counter; Advance() (called once per frame after the in-flight fence wait) runs every
	// entry whose stamp is at least `framesInFlight` frames old, i.e. every frame that could have referenced
	// the resource has been waited on. While disabled (before init, after the shutdown drain) Retire runs the
	// callback immediately: the device is idle or gone, so there is nothing to wait for.
	class DeferredDeletionQueue
	{
	public:
		using Fn = std::function<void()>;

		void Enable(const uint32_t framesInFlight)
		{
			std::scoped_lock lock(m_Mutex);
			m_FramesInFlight = framesInFlight;
			m_Enabled = true;
		}

		void Retire(Fn fn)
		{
			{
				std::scoped_lock lock(m_Mutex);
				if (m_Enabled)
				{
					m_Entries.push_back({m_Frame + m_FramesInFlight, std::move(fn)});
					return;
				}
			}
			fn();
		}

		void Advance()
		{
			std::vector<Fn> ready;
			{
				std::scoped_lock lock(m_Mutex);
				++m_Frame;
				std::erase_if(m_Entries,
				              [&](Entry& e)
				              {
					              if (e.ReadyFrame > m_Frame)
					              {
						              return false;
					              }
					              ready.push_back(std::move(e.Destroy));
					              return true;
				              });
			}
			for (Fn& fn : ready)
			{
				fn();
			}
		}

		// Caller guarantees the device is idle. Runs everything pending and goes back to immediate mode.
		void FlushAndDisable()
		{
			std::vector<Entry> pending;
			{
				std::scoped_lock lock(m_Mutex);
				pending.swap(m_Entries);
				m_Enabled = false;
			}
			for (Entry& e : pending)
			{
				e.Destroy();
			}
		}

		[[nodiscard]] size_t Size() const
		{
			std::scoped_lock lock(m_Mutex);
			return m_Entries.size();
		}

	private:
		struct Entry
		{
			uint64_t ReadyFrame;
			Fn Destroy;
		};

		mutable std::mutex m_Mutex;
		std::vector<Entry> m_Entries;
		uint64_t m_Frame = 0;
		uint32_t m_FramesInFlight = 0;
		bool m_Enabled = false;
	};
}
