#include "JobSystem.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"

namespace Snowstorm
{
	JobSystem::JobSystem()
	{
		// Leave one hardware thread for the main/render thread; always spawn at least one worker so Submit
		// still makes progress on single/dual-core machines (or when hardware_concurrency() reports 0).
		const unsigned hw = std::thread::hardware_concurrency();
		const size_t workerCount = (hw > 1) ? (hw - 1) : 1;

		m_Workers.reserve(workerCount);
		for (size_t i = 0; i < workerCount; ++i)
		{
			m_Workers.emplace_back([this]
			                       { WorkerLoop(); });
		}

		SS_CORE_INFO("JobSystem: {} worker thread(s).", workerCount);
	}

	JobSystem::~JobSystem()
	{
		// Signal shutdown, wake every worker, join. Tasks already queued still run (drained by the loop
		// before it sees an empty queue + stopping); no task is dropped. Futures for those complete normally.
		{
			std::lock_guard lock(m_Mutex);
			m_Stopping = true;
		}
		m_Condition.notify_all();

		for (std::thread& worker : m_Workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}

	void JobSystem::WorkerLoop()
	{
		for (;;)
		{
			std::function<void()> task;
			{
				std::unique_lock lock(m_Mutex);
				m_Condition.wait(lock, [this]
				                 { return m_Stopping || !m_Tasks.empty(); });

				// Exit only once there is no work left — drain the queue on shutdown so submitted tasks
				// (and their futures) always complete.
				if (m_Stopping && m_Tasks.empty())
				{
					return;
				}

				task = std::move(m_Tasks.front());
				m_Tasks.pop();
			}

			// Timeline event per worker task — this is what makes the cross-thread capture show worker
			// overlap vs. the main thread. (Generic label for now; threading a per-submit name through
			// Submit() is a follow-up.)
			SS_PROFILE_SCOPE("JobTask");
			task(); // packaged_task captures exceptions into its future; never throws out here.
		}
	}
}
