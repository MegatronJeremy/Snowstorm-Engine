#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/FrameRingRule.hpp"

using namespace Snowstorm;

TEST_CASE("Frame index wraps at frames in flight", "[render][framering]")
{
	CHECK(FrameRing::NextFrameIndex(0, 2) == 1);
	CHECK(FrameRing::NextFrameIndex(1, 2) == 0);
	CHECK(FrameRing::NextFrameIndex(2, 3) == 0);
}

TEST_CASE("One frame in flight stays on slot 0", "[render][framering]")
{
	CHECK(FrameRing::NextFrameIndex(0, 1) == 0);
}

TEST_CASE("Zero frames in flight does not divide by zero", "[render][framering]")
{
	CHECK(FrameRing::NextFrameIndex(0, 0) == 0);
}

TEST_CASE("Every index the advance produces addresses a ring", "[render][framering]")
{
	for (uint32_t frames = 1; frames <= 4; ++frames)
	{
		uint32_t index = 0;
		for (int i = 0; i < 10; ++i)
		{
			CHECK(FrameRing::IsValidSlot(index, frames));
			index = FrameRing::NextFrameIndex(index, frames);
		}
	}
}

TEST_CASE("Slot validity rejects the ring count and beyond", "[render][framering]")
{
	CHECK(FrameRing::IsValidSlot(1, 2));
	CHECK_FALSE(FrameRing::IsValidSlot(2, 2));
	CHECK_FALSE(FrameRing::IsValidSlot(0, 0));
}
