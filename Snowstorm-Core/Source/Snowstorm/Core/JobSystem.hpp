#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp" // SS_CORE_ASSERT expands to log macros; make the header self-contained
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

		// Data-parallel loop over [0, count): splits the range into chunks of ~grainSize and runs them
		// across the worker pool, blocking until all are done (the Unity DOTS / Unreal ParallelFor model).
		// `body(begin, end)` is invoked once per chunk with a half-open sub-range; it must be safe to run
		// concurrently on disjoint indices (the caller guarantees no data races between chunks).
		//
		// Degrades cleanly: if there's effectively no parallelism to be had (<=1 worker, count <= grainSize,
		// or grainSize == 0), the whole range runs inline on the calling thread with no task overhead — so
		// small N never pays the submit/sync cost. The last chunk also always runs inline so the calling
		// thread does useful work instead of just waiting.
		template <typename Fn>
		void ParallelFor(const size_t count, Fn&& body, const size_t grainSize = 256)
		{
			if (count == 0)
			{
				return;
			}

			// Inline fast path: nothing to gain from tasking.
			if (grainSize == 0 || count <= grainSize || m_Workers.size() <= 1)
			{
				body(size_t{0}, count);
				return;
			}

			const size_t chunkCount = (count + grainSize - 1) / grainSize;

			// Submit all chunks except the last; run the last inline on this thread while workers churn.
			std::vector<std::future<void>> futures;
			futures.reserve(chunkCount - 1);
			for (size_t c = 0; c + 1 < chunkCount; ++c)
			{
				const size_t begin = c * grainSize;
				const size_t end = begin + grainSize;
				futures.push_back(Submit([&body, begin, end]
				                         { body(begin, end); }));
			}

			const size_t lastBegin = (chunkCount - 1) * grainSize;
			body(lastBegin, count); // caller-thread chunk

			// Barrier: wait for every worker chunk. get() also propagates any exception a chunk threw.
			for (std::future<void>& f : futures)
			{
				f.get();
			}
		}

	private:
		void WorkerLoop();

		std::vector<std::thread> m_Workers;
		std::queue<std::function<void()>> m_Tasks;

		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Stopping = false;
	};
}
