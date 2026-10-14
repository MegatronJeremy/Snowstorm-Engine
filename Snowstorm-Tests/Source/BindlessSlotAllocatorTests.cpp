#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/BindlessSlotAllocator.hpp"

using namespace Snowstorm;

TEST_CASE("bindless allocator hands out sequential indices from firstIndex", "[bindless]")
{
	BindlessSlotAllocator a(8, 1, 2);
	REQUIRE(a.Allocate() == 1);
	REQUIRE(a.Allocate() == 2);
	REQUIRE(a.InUse() == 2);
}

TEST_CASE("bindless allocator quarantines released slots for the retire delay", "[bindless]")
{
	BindlessSlotAllocator a(8, 0, 2);
	const uint32_t s0 = a.Allocate();
	const uint32_t s1 = a.Allocate();
	a.Release(s0);

	REQUIRE(a.Allocate() == s1 + 1); // not reusable yet
	a.Advance();
	REQUIRE(a.Allocate() == s1 + 2); // one frame of two
	a.Advance();
	REQUIRE(a.Allocate() == s0); // retired
}

TEST_CASE("bindless allocator reuse keeps a churn loop inside the array", "[bindless]")
{
	BindlessSlotAllocator a(4, 0, 1);
	for (int i = 0; i < 100; ++i)
	{
		const uint32_t s = a.Allocate();
		REQUIRE(s != BindlessSlotAllocator::Invalid);
		a.Release(s);
		a.Advance();
	}
	REQUIRE(a.InUse() <= 2);
}

TEST_CASE("bindless allocator reports exhaustion and ignores sentinel releases", "[bindless]")
{
	BindlessSlotAllocator a(2, 1, 0);
	REQUIRE(a.Allocate() == 1);
	REQUIRE(a.Allocate() == BindlessSlotAllocator::Invalid);

	a.Release(0); // sentinel
	a.Release(7); // never handed out
	a.Advance();
	REQUIRE(a.Allocate() == BindlessSlotAllocator::Invalid);

	a.Release(1);
	a.Advance();
	REQUIRE(a.Allocate() == 1);
}
