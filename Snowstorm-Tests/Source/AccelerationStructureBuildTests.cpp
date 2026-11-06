#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/AccelerationStructureBuild.hpp"

using namespace Snowstorm;

TEST_CASE("AccelerationStructureBuild: default options are today's PREFER_FAST_TRACE", "[accel]")
{
	REQUIRE(AsBuildFlagBits({}) == 0x4); // VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
}

TEST_CASE("AccelerationStructureBuild: flag bits match the Vulkan values", "[accel]")
{
	REQUIRE(AsBuildFlagBits({AsBuildPreference::FastBuild, false}) == 0x8);
	REQUIRE(AsBuildFlagBits({AsBuildPreference::FastTrace, true}) == (0x4 | 0x2));
	REQUIRE(AsBuildFlagBits({AsBuildPreference::FastBuild, true}) == (0x8 | 0x2));
}

TEST_CASE("AccelerationStructureBuild: compaction needs a nonzero, strictly smaller size", "[accel]")
{
	REQUIRE(ShouldCompactAs(1000, 600));
	REQUIRE_FALSE(ShouldCompactAs(1000, 1000));
	REQUIRE_FALSE(ShouldCompactAs(1000, 1200));
	REQUIRE_FALSE(ShouldCompactAs(1000, 0));
}
