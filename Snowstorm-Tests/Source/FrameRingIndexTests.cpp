#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/FrameRingIndex.hpp"

using namespace Snowstorm;

TEST_CASE("FrameRingIndex: frames in flight must be positive", "[render][frame-ring]")
{
	CHECK_FALSE(IsValidFramesInFlight(0));
	CHECK(IsValidFramesInFlight(1));
	CHECK(IsValidFramesInFlight(2));
	CHECK(IsValidFramesInFlight(3));
}

TEST_CASE("FrameRingIndex: index is valid below the ring count only", "[render][frame-ring]")
{
	CHECK(IsValidFrameRingIndex(0, 2));
	CHECK(IsValidFrameRingIndex(1, 2));
	CHECK_FALSE(IsValidFrameRingIndex(2, 2));
	CHECK_FALSE(IsValidFrameRingIndex(UINT32_MAX, 2));
}

TEST_CASE("FrameRingIndex: a single frame in flight accepts only index 0", "[render][frame-ring]")
{
	CHECK(IsValidFrameRingIndex(0, 1));
	CHECK_FALSE(IsValidFrameRingIndex(1, 1));
}

TEST_CASE("FrameRingIndex: an empty ring set accepts nothing", "[render][frame-ring]")
{
	CHECK_FALSE(IsValidFrameRingIndex(0, 0));
}
