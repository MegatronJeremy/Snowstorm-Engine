#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/DescriptorPoolPolicy.hpp"

using namespace Snowstorm;

TEST_CASE("pool set count doubles from 64 and caps at 1024", "[descriptorpool]")
{
	REQUIRE(DescriptorPoolSetCount(0) == 64);
	REQUIRE(DescriptorPoolSetCount(1) == 128);
	REQUIRE(DescriptorPoolSetCount(4) == 1024);
	REQUIRE(DescriptorPoolSetCount(5) == 1024);
	REQUIRE(DescriptorPoolSetCount(1000) == 1024);
}

TEST_CASE("pool key compares by shape", "[descriptorpool]")
{
	DescriptorPoolKey a, b;
	a.Add(0, 1);
	a.Add(2, 3);
	b.Add(2, 2);
	b.Add(2, 1);
	b.Add(0, 1);
	REQUIRE(a == b);

	b.Add(1, 1);
	REQUIRE_FALSE(a == b);

	DescriptorPoolKey c = a;
	c.Add(DescriptorPoolKey::kTypeSlots, 5); // out-of-range slot is ignored, never written
	REQUIRE(a == c);
}

TEST_CASE("pool book fills a pool before asking for another", "[descriptorpool]")
{
	DescriptorPoolBook book;
	REQUIRE(book.Acquire() == DescriptorPoolBook::Invalid);

	const uint32_t p0 = book.AddPool();
	for (uint32_t i = 0; i < 64; ++i)
	{
		REQUIRE(book.Acquire() == p0);
	}
	REQUIRE(book.Acquire() == DescriptorPoolBook::Invalid);

	const uint32_t p1 = book.AddPool();
	REQUIRE(book.MaxSets(p1) == 128);
	REQUIRE(book.Acquire() == p1);
	REQUIRE(book.TotalLive() == 65);
}

TEST_CASE("released set makes its pool available again", "[descriptorpool]")
{
	DescriptorPoolBook book;
	const uint32_t p0 = book.AddPool();
	for (uint32_t i = 0; i < 64; ++i)
	{
		book.Acquire();
	}
	book.Release(p0);
	REQUIRE(book.Acquire() == p0);
	REQUIRE(book.Live(p0) == 64);
}

TEST_CASE("driver-exhausted pool is skipped until a release", "[descriptorpool]")
{
	DescriptorPoolBook book;
	const uint32_t p0 = book.AddPool();
	REQUIRE(book.Acquire() == p0);
	book.MarkExhausted(p0); // fragmented: the driver refused despite counted room
	REQUIRE(book.Live(p0) == 0);
	REQUIRE(book.Acquire() == DescriptorPoolBook::Invalid);

	const uint32_t p1 = book.AddPool();
	REQUIRE(book.Acquire() == p1);
}

TEST_CASE("retire queue holds items for the delay", "[descriptorpool]")
{
	DescriptorRetireQueue<int> q(3);
	std::vector<int> ready;
	q.Retire(7);
	q.Advance(ready);
	q.Retire(8);
	q.Advance(ready);
	REQUIRE(ready.empty());
	q.Advance(ready);
	REQUIRE(ready == std::vector<int>{7});
	q.Advance(ready);
	REQUIRE(ready == std::vector<int>{7, 8});
	REQUIRE(q.Pending() == 0);
}

TEST_CASE("retire queue drain releases everything", "[descriptorpool]")
{
	DescriptorRetireQueue<int> q(3);
	q.Retire(1);
	q.Retire(2);
	std::vector<int> ready;
	q.Drain(ready);
	REQUIRE(ready == std::vector<int>{1, 2});
	REQUIRE(q.Pending() == 0);
}
