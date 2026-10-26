#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/WorkgroupSize.hpp"

using namespace Snowstorm;

TEST_CASE("WorkgroupSize: only 4, 8 and 16 are selectable", "[shader]")
{
	REQUIRE(ClampWorkgroupSide(4) == 4);
	REQUIRE(ClampWorkgroupSide(8) == 8);
	REQUIRE(ClampWorkgroupSide(16) == 16);
	for (const int bad : {-1, 0, 1, 7, 12, 32, 64})
	{
		REQUIRE(ClampWorkgroupSide(bad) == kDefaultWorkgroupSide);
	}
}

TEST_CASE("WorkgroupSize: group count rounds up and matches the old (n + 7) / 8 at the default", "[shader]")
{
	for (uint32_t n : {1u, 7u, 8u, 9u, 1280u, 1281u, 1920u, 1080u})
	{
		REQUIRE(WorkgroupCount(n, 8) == (n + 7) / 8);
	}
	REQUIRE(WorkgroupCount(1080, 16) == 68);
	REQUIRE(WorkgroupCount(1080, 4) == 270);
	REQUIRE(WorkgroupCount(17, 16) == 2);
	REQUIRE(WorkgroupCount(17, 99) == 3); // invalid side dispatches as the default
}

TEST_CASE("WorkgroupSize: the default adds no defines, others add X and Y", "[shader]")
{
	std::vector<std::string> d;
	AppendWorkgroupDefines(d, 8);
	AppendWorkgroupDefines(d, 5); // invalid -> default
	REQUIRE(d.empty());
	AppendWorkgroupDefines(d, 16);
	REQUIRE(d == std::vector<std::string>{"SS_WG_X=16", "SS_WG_Y=16"});
}
