#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/QualityCaptureSchedule.hpp"

using namespace Snowstorm::QualityCaptureSchedule;

TEST_CASE("Route frame list parses, sorts and reports malformed entries", "[quality]")
{
	const RouteFrames r = ParseRouteFrames("420, 421,120 ,121,abc,,  ,7x");
	REQUIRE(r.Frames == std::vector<uint64_t>{7, 120, 121, 420, 421});
	REQUIRE(r.Rejected == std::vector<std::string>{"abc"});
}

TEST_CASE("Route frame list: empty input yields nothing", "[quality]")
{
	REQUIRE(ParseRouteFrames("").Frames.empty());
	REQUIRE(ParseRouteFrames(",,").Rejected.empty());
}

TEST_CASE("Route frame list keeps frame 0 and duplicates", "[quality]")
{
	const RouteFrames r = ParseRouteFrames("0,0,5");
	REQUIRE(r.Frames == std::vector<uint64_t>{0, 0, 5});
}

TEST_CASE("Frame suffix is zero-padded to six digits and sorts lexicographically", "[quality]")
{
	REQUIRE(FrameSuffix(0) == "000000");
	REQUIRE(FrameSuffix(120) == "000120");
	REQUIRE(FrameSuffix(999999) == "999999");
	REQUIRE(FrameSuffix(1234567) == "1234567");
	REQUIRE(FrameSuffix(99) < FrameSuffix(100));
}

TEST_CASE("Sequence step: nothing happens before the route flies and assets settle", "[quality]")
{
	const std::vector<uint64_t> wanted{0, 5};
	REQUIRE_FALSE(DecideSequenceStep(wanted, 0, 0, false, true).Capture);
	REQUIRE_FALSE(DecideSequenceStep(wanted, 0, 0, true, false).Capture);
	REQUIRE(DecideSequenceStep(wanted, 0, 9, false, false).Missed == 0);
}

TEST_CASE("Sequence step: capture on an exact match, frame 0 included", "[quality]")
{
	const std::vector<uint64_t> wanted{0, 5};
	const SequenceStep s = DecideSequenceStep(wanted, 0, 0, true, true);
	REQUIRE(s.Capture);
	REQUIRE(s.Missed == 0);
	REQUIRE_FALSE(DecideSequenceStep(wanted, 1, 4, true, true).Capture);
	REQUIRE(DecideSequenceStep(wanted, 1, 5, true, true).Capture);
}

TEST_CASE("Sequence step: passed requests are counted as missed", "[quality]")
{
	const std::vector<uint64_t> wanted{120, 121, 420};
	SequenceStep s = DecideSequenceStep(wanted, 0, 121, true, true);
	REQUIRE(s.Missed == 1);
	REQUIRE(s.Capture);
	s = DecideSequenceStep(wanted, 0, 500, true, true);
	REQUIRE(s.Missed == 3);
	REQUIRE_FALSE(s.Capture);
}

TEST_CASE("Sequence step: an exhausted list neither captures nor misses", "[quality]")
{
	const std::vector<uint64_t> wanted{1, 2};
	const SequenceStep s = DecideSequenceStep(wanted, 2, 50, true, true);
	REQUIRE(s.Missed == 0);
	REQUIRE_FALSE(s.Capture);
}
