#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/JobSystem.hpp"

#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

using namespace Snowstorm;

// ParallelFor is the primitive the data-parallel ECS is built on, so the contract that matters is:
// every index in [0,count) is visited exactly once, chunks are disjoint, and the result equals the
// serial computation — for counts that are and aren't multiples of the grain, and for tiny counts that
// take the inline fast path.

TEST_CASE("ParallelFor visits every index exactly once", "[jobsystem]")
{
	JobSystem jobs;

	for (const size_t count : {size_t{0}, size_t{1}, size_t{255}, size_t{256}, size_t{257}, size_t{10000}})
	{
		std::vector<int> hits(count, 0);
		jobs.ParallelFor(
		    count,
		    [&](const size_t begin, const size_t end)
		    {
			    for (size_t i = begin; i < end; ++i)
			    {
				    hits[i] += 1;
			    }
		    },
		    64);

		for (size_t i = 0; i < count; ++i)
		{
			REQUIRE(hits[i] == 1); // exactly once — no gaps, no double-processing
		}
	}
}

TEST_CASE("ParallelFor result matches serial computation", "[jobsystem]")
{
	JobSystem jobs;
	constexpr size_t count = 50000;

	std::vector<uint64_t> data(count);
	std::iota(data.begin(), data.end(), 0);

	// Square each element in parallel (disjoint writes), then compare the sum to the serial answer.
	jobs.ParallelFor(
	    count,
	    [&](const size_t begin, const size_t end)
	    {
		    for (size_t i = begin; i < end; ++i)
		    {
			    data[i] = data[i] * data[i];
		    }
	    },
	    128);

	uint64_t parallelSum = 0;
	for (const uint64_t v : data)
	{
		parallelSum += v;
	}

	uint64_t serialSum = 0;
	for (uint64_t i = 0; i < count; ++i)
	{
		serialSum += i * i;
	}

	REQUIRE(parallelSum == serialSum);
}

TEST_CASE("ParallelFor with grainSize >= count runs inline without error", "[jobsystem]")
{
	JobSystem jobs;
	std::atomic<int> calls{0};
	jobs.ParallelFor(
	    100, [&](const size_t, const size_t)
	    { calls.fetch_add(1); }, 1000 /* grain > count -> inline */);
	REQUIRE(calls.load() == 1); // single inline invocation over the whole range
}

// ParallelGather is the primitive VisibilitySystem's parallel frustum cull is built on. The contract:
// every emitted value appears, in INDEX order (deterministic regardless of scheduling), matching a
// serial copy_if exactly — for counts that are and aren't multiples of the grain, and for the inline
// fast path.
TEST_CASE("ParallelGather matches serial copy_if in value and order", "[jobsystem]")
{
	JobSystem jobs;

	for (const size_t count : {size_t{0}, size_t{1}, size_t{255}, size_t{256}, size_t{257}, size_t{10000}})
	{
		// Gather the even numbers in [0, count). Emitting the index itself makes the expected order the
		// natural ascending order, so any reordering by the parallel merge would show up immediately.
		const std::vector<size_t> got = jobs.ParallelGather<size_t>(
		    count,
		    [](const size_t i, auto&& emit)
		    {
			    if (i % 2 == 0)
			    {
				    emit(i);
			    }
		    },
		    64);

		std::vector<size_t> expected;
		for (size_t i = 0; i < count; ++i)
		{
			if (i % 2 == 0)
			{
				expected.push_back(i);
			}
		}

		REQUIRE(got == expected); // same values AND same order as the serial computation
	}
}

TEST_CASE("ParallelGather is deterministic across repeated runs", "[jobsystem]")
{
	JobSystem jobs;
	constexpr size_t count = 50000;

	const auto run = [&jobs]
	{
		return jobs.ParallelGather<size_t>(
		    count,
		    [](const size_t i, auto&& emit)
		    {
			    if (i % 3 == 0)
			    {
				    emit(i * 2); // emit a transformed value, not just a filter
			    }
		    },
		    128);
	};

	const std::vector<size_t> a = run();
	const std::vector<size_t> b = run();
	REQUIRE(a == b); // scheduling must not change the result (order or contents)
}

// WaitAll is the drain primitive that closes the project-switch use-after-free: async asset loads
// capture the (World-scoped) AssetManagerSingleton and write member state on completion, so the World
// must not be destroyed while any worker is still running. The contract that matters: after WaitAll
// returns, EVERY submitted task has fully finished — including tasks still executing on a worker when
// WaitAll was called (queue-empty alone is not enough; a popped-but-running task must be waited on too).
TEST_CASE("WaitAll blocks until every submitted task has completed", "[jobsystem]")
{
	JobSystem jobs;
	constexpr int taskCount = 64;

	std::atomic<int> completed{0};
	for (int i = 0; i < taskCount; ++i)
	{
		(void)jobs.Submit([&completed]
		                  {
			// Sleep so tasks are still mid-execution when WaitAll is called — this is what exercises the
			// "popped from queue but not yet done" window that queue-empty checking would miss.
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			completed.fetch_add(1, std::memory_order_relaxed); });
	}

	jobs.WaitAll();

	// If WaitAll returned while any task was still running, this would flake below taskCount.
	REQUIRE(completed.load(std::memory_order_relaxed) == taskCount);
}

TEST_CASE("WaitAll on an idle pool returns immediately", "[jobsystem]")
{
	JobSystem jobs;
	jobs.WaitAll(); // nothing submitted -> must not hang
	SUCCEED();
}

TEST_CASE("ParallelGather can emit multiple values per index", "[jobsystem]")
{
	JobSystem jobs;
	constexpr size_t count = 1000;

	// Each index emits itself twice -> result is [0,0,1,1,2,2,...] in order.
	const std::vector<size_t> got = jobs.ParallelGather<size_t>(
	    count,
	    [](const size_t i, auto&& emit)
	    {
		    emit(i);
		    emit(i);
	    },
	    64);

	REQUIRE(got.size() == count * 2);
	for (size_t i = 0; i < count; ++i)
	{
		REQUIRE(got[i * 2] == i);
		REQUIRE(got[i * 2 + 1] == i);
	}
}
