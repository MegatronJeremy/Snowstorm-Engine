#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/CompileGate.hpp"

#include <atomic>
#include <thread>
#include <vector>

using namespace Snowstorm;

TEST_CASE("CompileGate: idle request owns the chain, finish returns to idle", "[shader]")
{
	CompileGate gate;
	REQUIRE(gate.Request());
	REQUIRE_FALSE(gate.Finish());
	REQUIRE(gate.Request());
	REQUIRE_FALSE(gate.Finish());
}

TEST_CASE("CompileGate: requests during a compile coalesce into exactly one rerun", "[shader]")
{
	CompileGate gate;
	REQUIRE(gate.Request());
	REQUIRE_FALSE(gate.Request());
	REQUIRE_FALSE(gate.Request());
	REQUIRE_FALSE(gate.Request());
	REQUIRE(gate.Finish());
	REQUIRE_FALSE(gate.Finish());
}

TEST_CASE("CompileGate: a request during a rerun is not lost", "[shader]")
{
	CompileGate gate;
	REQUIRE(gate.Request());
	REQUIRE_FALSE(gate.Request());
	REQUIRE(gate.Finish());
	REQUIRE_FALSE(gate.Request());
	REQUIRE(gate.Finish());
	REQUIRE_FALSE(gate.Finish());
}

TEST_CASE("CompileGate: concurrent requests never overlap compiles and the last input is compiled", "[shader]")
{
	CompileGate gate;
	std::atomic<int> input{0};
	std::atomic<int> running{0};
	std::atomic<int> maxRunning{0};
	std::atomic<int> compiles{0};
	std::atomic<int> published{-1};

	auto request = [&](int value)
	{
		input.store(value);
		if (!gate.Request())
			return;
		do
		{
			const int now = ++running;
			int m = maxRunning.load();
			while (now > m && !maxRunning.compare_exchange_weak(m, now))
			{
			}
			const int seen = input.load();
			std::this_thread::yield();
			published.store(seen);
			++compiles;
			--running;
		} while (gate.Finish());
	};

	constexpr int kThreads = 8;
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; ++t)
		threads.emplace_back([&, t]
		                     { for (int i = 0; i < 200; ++i) request(t * 1000 + i); });
	for (auto& th : threads)
		th.join();

	REQUIRE(maxRunning.load() == 1);
	REQUIRE(compiles.load() >= 1);
	REQUIRE(published.load() == input.load());
}
