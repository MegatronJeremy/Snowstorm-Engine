#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/DeferredDeletionQueue.hpp"

using namespace Snowstorm;

TEST_CASE("deferred deletion runs immediately while disabled", "[deferred-delete]")
{
	DeferredDeletionQueue q;
	int ran = 0;
	q.Retire([&]
	         { ++ran; });
	REQUIRE(ran == 1);
	REQUIRE(q.Size() == 0);
}

TEST_CASE("deferred deletion waits framesInFlight advances", "[deferred-delete]")
{
	DeferredDeletionQueue q;
	q.Enable(2);
	int ran = 0;
	q.Retire([&]
	         { ++ran; });
	REQUIRE(ran == 0);
	q.Advance();
	REQUIRE(ran == 0);
	q.Advance();
	REQUIRE(ran == 1);
	REQUIRE(q.Size() == 0);
}

TEST_CASE("deferred deletion stamps each entry at its own retire frame", "[deferred-delete]")
{
	DeferredDeletionQueue q;
	q.Enable(2);
	int a = 0, b = 0;
	q.Retire([&]
	         { ++a; });
	q.Advance();
	q.Retire([&]
	         { ++b; });
	q.Advance();
	REQUIRE(a == 1);
	REQUIRE(b == 0);
	q.Advance();
	REQUIRE(b == 1);
}

TEST_CASE("flush runs pending entries and returns to immediate mode", "[deferred-delete]")
{
	DeferredDeletionQueue q;
	q.Enable(3);
	int ran = 0;
	q.Retire([&]
	         { ++ran; });
	q.FlushAndDisable();
	REQUIRE(ran == 1);
	q.Retire([&]
	         { ++ran; });
	REQUIRE(ran == 2);
}
