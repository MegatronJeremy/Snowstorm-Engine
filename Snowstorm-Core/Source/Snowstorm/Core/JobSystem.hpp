#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Snowstorm
{
	// Application-scoped thread pool: the engine's foundation for off-main-thread work (async asset
	// loading, and later parallel ECS system execution). A fixed set of worker threads pull tasks off a
	// shared queue; Submit() returns a std::future so callers can join a specific result.
	//
	// Design (deliberately minimal, cf. a stripped-down enkiTS / Unity job queue): one mutex-guarded FIFO
	// queue + a condition variable. No work-stealing, no priorities, no fibers — those are the upgrade path
	// if profiling ever shows the single queue is a bottleneck. Worker count defaults to
	// hardware_concurrency()-1 (leave a core for the main/render thread).
	//
	// Threading contract: submitted tasks run on worker threads, so anything they touch must be safe to use
	// off the main thread. In particular GPU resource creation (Vulkan) MUST stay on the main thread — the
	// intended pattern is "cook/parse CPU data on a worker, then create GPU buffers on the main thread from
	// the result" (see the async-load follow-up).
	class JobSystem final : public Service
	{
	public:
		JobSystem();
		~JobSystem() override;

		// Enqueue a task; returns a future that becomes ready when the task has run. The callable is moved
		// onto the queue. Exceptions thrown by the task propagate through future::get (packaged_task).
		template <typename Fn>
		auto Submit(Fn&& fn) -> std::future<std::invoke_result_t<Fn>>
		{
			using Result = std::invoke_result_t<Fn>;

			auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Fn>(fn));
			std::future<Result> future = task->get_future();

			{
				std::lock_guard lock(m_Mutex);
				SS_CORE_ASSERT(!m_Stopping, "JobSystem::Submit after shutdown");
				m_Tasks.emplace([task]()
				                { (*task)(); });
			}
			m_Condition.notify_one();
			return future;
		}

		// Number of worker threads in the pool (>= 1).
		[[nodiscard]] size_t WorkerCount() const { return m_Workers.size(); }

	private:
		void WorkerLoop();

		std::vector<std::thread> m_Workers;
		std::queue<std::function<void()>> m_Tasks;

		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Stopping = false;
	};
}
