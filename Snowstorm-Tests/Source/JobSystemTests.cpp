#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/JobSystem.hpp"

#include <atomic>
#include <numeric>
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
