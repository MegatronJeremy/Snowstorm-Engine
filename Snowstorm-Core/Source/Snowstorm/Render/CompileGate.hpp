#pragma once

#include <atomic>
#include <cstdint>

namespace Snowstorm
{
	// Serialises compiles of one shader and coalesces requests that arrive while one runs: at most one
	// compile chain is live, and a request made mid-compile guarantees exactly one more run that reads the
	// inputs as they are then, so the last request always wins.
	//   if (gate.Request()) { do { Compile(); } while (gate.Finish()); }
	class CompileGate
	{
	public:
		// True: the caller owns the chain and must loop on Finish(). False: a chain is running and has
		// been told to run once more.
		[[nodiscard]] bool Request()
		{
			uint32_t s = m_State.load(std::memory_order_acquire);
			for (;;)
			{
				const uint32_t next = (s == Idle) ? Running : RunningDirty;
				if (m_State.compare_exchange_weak(s, next, std::memory_order_acq_rel))
					return s == Idle;
			}
		}

		// True: a request arrived during the compile, run again. False: the gate is idle again.
		[[nodiscard]] bool Finish()
		{
			uint32_t s = RunningDirty;
			if (m_State.compare_exchange_strong(s, Running, std::memory_order_acq_rel))
				return true;
			m_State.store(Idle, std::memory_order_release);
			return false;
		}

	private:
		enum : uint32_t
		{
			Idle,
			Running,
			RunningDirty
		};
		std::atomic<uint32_t> m_State{Idle};
	};
}
