#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/GpuZoneTimeline.hpp"

using namespace Snowstorm;

namespace
{
	GpuScope MakeScope(const char* name, uint32_t depth, uint64_t s, uint64_t e, GpuQueue q = GpuQueue::Graphics)
	{
		GpuScope g;
		g.Name = name;
		g.Depth = depth;
		g.StartNs = s;
		g.EndNs = e;
		g.Queue = q;
		return g;
	}
}

TEST_CASE("GPU zone events nest parent over child and sort by time", "[render][gpu]")
{
	// Sky is a child of Forward and shares both its start and its end.
	const std::vector<GpuScope> scopes = {MakeScope("Forward", 0, 100, 200), MakeScope("Sky", 1, 100, 200),
	                                      MakeScope("Post", 0, 200, 300)};
	const auto ev = BuildGpuZoneEvents(scopes, GpuQueue::Graphics);
	REQUIRE(ev.size() == 6);
	const uint32_t scopeOrder[6] = {0, 1, 1, 0, 2, 2};
	const bool beginOrder[6] = {true, true, false, false, true, false};
	for (size_t i = 0; i < 6; ++i)
	{
		CHECK(ev[i].Scope == scopeOrder[i]);
		CHECK(ev[i].Begin == beginOrder[i]);
	}
}

TEST_CASE("GPU zone events keep a zero-length scope begin before its end", "[render][gpu]")
{
	const auto ev = BuildGpuZoneEvents({MakeScope("Empty", 0, 50, 50)}, GpuQueue::Graphics);
	REQUIRE(ev.size() == 2);
	CHECK(ev[0].Begin);
	CHECK_FALSE(ev[1].Begin);
}

TEST_CASE("GPU zone events filter by queue and drop unrecorded scopes", "[render][gpu]")
{
	const std::vector<GpuScope> scopes = {MakeScope("Gfx", 0, 10, 20), MakeScope("Comp", 0, 12, 18, GpuQueue::AsyncCompute),
	                                      MakeScope("None", 0, 0, 0)};
	CHECK(BuildGpuZoneEvents(scopes, GpuQueue::Graphics).size() == 2);
	CHECK(BuildGpuZoneEvents(scopes, GpuQueue::AsyncCompute).size() == 2);
}
