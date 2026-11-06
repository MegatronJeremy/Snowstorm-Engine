#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "Snowstorm/Render/UniformRingMath.hpp"

using namespace Snowstorm;

TEST_CASE("ring alignment rounds the head up to a power of two", "[uniformring]")
{
	REQUIRE(RingAlignUp(0, 256) == 0);
	REQUIRE(RingAlignUp(1, 256) == 256);
	REQUIRE(RingAlignUp(256, 256) == 256);
	REQUIRE(RingAlignUp(257, 256) == 512);
	REQUIRE(RingAlignUp(5, 1) == 5);
	REQUIRE(RingAlignUp(5, 4) == 8);
}

TEST_CASE("ring allocations are placed at the aligned head", "[uniformring]")
{
	auto r = RingAllocate(0, 1024, 100, 256);
	REQUIRE(r.Status == RingAllocStatus::Fits);
	REQUIRE(r.Offset == 0);

	r = RingAllocate(100, 1024, 100, 256);
	REQUIRE(r.Status == RingAllocStatus::Fits);
	REQUIRE(r.Offset == 256);
	REQUIRE(r.Offset % 256 == 0);
}

TEST_CASE("ring allocation may end exactly at capacity", "[uniformring]")
{
	const auto r = RingAllocate(256, 1024, 768, 256);
	REQUIRE(r.Status == RingAllocStatus::Fits);
	REQUIRE(r.Offset == 256);
}

TEST_CASE("ring overflow clamps flush with the end of the ring", "[uniformring]")
{
	// aligned head 768 + 512 > 1024
	const auto r = RingAllocate(600, 1024, 512, 256);
	REQUIRE(r.Status == RingAllocStatus::Clamped);
	REQUIRE(r.Offset == 512);
	REQUIRE(r.Offset + 512 == 1024);

	// alignment padding alone pushes a request past the end
	const auto p = RingAllocate(1, 1024, 1024, 256);
	REQUIRE(p.Status == RingAllocStatus::Clamped);
	REQUIRE(p.Offset == 0);
}

TEST_CASE("ring rejects a request larger than the whole ring", "[uniformring]")
{
	const auto r = RingAllocate(0, 1024, 1025, 16);
	REQUIRE(r.Status == RingAllocStatus::TooLarge);
	REQUIRE(r.Offset == 0);

	REQUIRE(RingAllocate(0, 1024, 1024, 16).Status == RingAllocStatus::Fits);
}

TEST_CASE("ring head after a frame reset allocates from zero again", "[uniformring]")
{
	// BeginFrame sets the head to 0; a ring full before it accepts the same request after it.
	REQUIRE(RingAllocate(1024, 1024, 64, 64).Status == RingAllocStatus::Clamped);
	const auto r = RingAllocate(0, 1024, 64, 64);
	REQUIRE(r.Status == RingAllocStatus::Fits);
	REQUIRE(r.Offset == 0);
}

TEST_CASE("ring overflow test does not wrap in 32 bits", "[uniformring]")
{
	const uint32_t big = 0xFFFFFF00u;
	const auto r = RingAllocate(big, 1024, 512, 16);
	REQUIRE(r.Status == RingAllocStatus::Clamped);
	REQUIRE(r.Offset == 512);
}
