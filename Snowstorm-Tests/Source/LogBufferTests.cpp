#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/LogBuffer.hpp"

#include <thread>
#include <vector>

using namespace Snowstorm;

// The console reads LogBuffer::Snapshot() while log calls (main thread + JobSystem workers) push into it,
// so the guarantees that matter are: pushes are captured in order, and concurrent pushes don't corrupt it.

TEST_CASE("LogBuffer captures pushed entries with level + text", "[log]")
{
	auto& buf = LogBuffer::Get();
	buf.Clear();

	buf.Push(LogBuffer::Level::Info, "hello");
	buf.Push(LogBuffer::Level::Warn, "careful");
	buf.Push(LogBuffer::Level::Error, "boom");

	const auto snap = buf.Snapshot();
	REQUIRE(snap.size() == 3);
	REQUIRE(snap[0].Text == "hello");
	REQUIRE(snap[0].LevelValue == LogBuffer::Level::Info);
	REQUIRE(snap[2].LevelValue == LogBuffer::Level::Error);
}

TEST_CASE("LogBuffer Revision advances on push and clear", "[log]")
{
	auto& buf = LogBuffer::Get();
	buf.Clear();
	const uint64_t r0 = buf.Revision();
	buf.Push(LogBuffer::Level::Info, "x");
	REQUIRE(buf.Revision() > r0);
	const uint64_t r1 = buf.Revision();
	buf.Clear();
	REQUIRE(buf.Revision() > r1);
}

TEST_CASE("LogBuffer survives concurrent pushes without corruption", "[log]")
{
	auto& buf = LogBuffer::Get();
	buf.Clear();

	constexpr int kThreads = 8;
	constexpr int kPerThread = 500;

	std::vector<std::thread> workers;
	workers.reserve(kThreads);
	for (int t = 0; t < kThreads; ++t)
	{
		workers.emplace_back([&buf]
		                     {
			for (int i = 0; i < kPerThread; ++i)
			{
				buf.Push(LogBuffer::Level::Info, "line");
			} });
	}
	for (auto& w : workers)
	{
		w.join();
	}

	// All pushes captured (total is under the 5000 capacity, so none evicted) and every entry is intact.
	const auto snap = buf.Snapshot();
	REQUIRE(snap.size() == static_cast<size_t>(kThreads) * kPerThread);
	for (const auto& e : snap)
	{
		REQUIRE(e.Text == "line");
	}

	buf.Clear();
}
