#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/SubgroupSizePolicy.hpp"

using namespace Snowstorm;

TEST_CASE("subgroup override matches by path substring, first match wins", "[subgroup]")
{
	REQUIRE(SubgroupOverrideFor("GIDenoise=32,Reflection=64", "Engine/Shaders/GIDenoise.comp.hlsl") == 32);
	REQUIRE(SubgroupOverrideFor("GIDenoise=32,Reflection=64", "Engine/Shaders/Reflection.comp.hlsl") == 64);
	REQUIRE(SubgroupOverrideFor("GI=64,GIDenoise=32", "Engine/Shaders/GIDenoise.comp.hlsl") == 64);
	REQUIRE(SubgroupOverrideFor("GIDenoise=32", "Engine/Shaders/AO.comp.hlsl") == 0);
	REQUIRE(SubgroupOverrideFor("", "Engine/Shaders/AO.comp.hlsl") == 0);
}

TEST_CASE("subgroup override skips malformed entries and keeps scanning", "[subgroup]")
{
	REQUIRE(SubgroupOverrideFor("=32,AO,AO=,AO=x,AO=0,AO=48,AO=-32,AO=99999,AO=16", "AO.comp") == 16);
	REQUIRE(SubgroupOverrideFor("AO=48", "AO.comp") == 0);
	REQUIRE(SubgroupOverrideFor(",,AO=32,,", "AO.comp") == 32);
}

TEST_CASE("required subgroup size: override beats global, device range bounds it", "[subgroup]")
{
	REQUIRE(ResolveRequiredSubgroupSize(0, "", "AO.comp", 32, 64, true) == 0);
	REQUIRE(ResolveRequiredSubgroupSize(32, "", "AO.comp", 32, 64, true) == 32);
	REQUIRE(ResolveRequiredSubgroupSize(32, "AO=64", "AO.comp", 32, 64, true) == 64);
	REQUIRE(ResolveRequiredSubgroupSize(32, "AO=64", "GI.comp", 32, 64, true) == 32);
}

TEST_CASE("required subgroup size: unsupported requests become no request", "[subgroup]")
{
	REQUIRE(ResolveRequiredSubgroupSize(64, "", "AO.comp", 32, 32, true) == 0);
	REQUIRE(ResolveRequiredSubgroupSize(16, "", "AO.comp", 32, 64, true) == 0);
	REQUIRE(ResolveRequiredSubgroupSize(48, "", "AO.comp", 16, 128, true) == 0);
	REQUIRE(ResolveRequiredSubgroupSize(-1, "", "AO.comp", 32, 64, true) == 0);
	REQUIRE(ResolveRequiredSubgroupSize(32, "AO=64", "AO.comp", 32, 64, false) == 0);
}
