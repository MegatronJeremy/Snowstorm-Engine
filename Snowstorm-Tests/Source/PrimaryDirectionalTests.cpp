#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Lighting/PrimaryDirectional.hpp"

#include <vector>

using namespace Snowstorm;

TEST_CASE("Primary directional is the first enabled light", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, true}, {true, true}};
	REQUIRE(FindPrimaryDirectional(lights) == 0);
}

TEST_CASE("A disabled first light is skipped (shader's DirectionalLights[0] is the second)", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{false, true}, {true, true}};
	REQUIRE(FindPrimaryDirectional(lights) == 1);
}

TEST_CASE("No enabled light yields size()", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> none;
	REQUIRE(FindPrimaryDirectional(none) == 0);
	const std::vector<DirectionalShadowCandidate> off{{false, true}, {false, false}};
	REQUIRE(FindPrimaryDirectional(off) == off.size());
}

TEST_CASE("Primary is picked regardless of CastShadows", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, false}, {true, true}};
	REQUIRE(FindPrimaryDirectional(lights) == 0);
}

TEST_CASE("Shadow slots: primary gets the sun map, secondaries get atlas tiles", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, true}, {true, true}, {true, true}};
	const auto sel = SelectShadowDirectionals(lights, 3, 3);
	REQUIRE(sel.Slots == std::vector<int>{0, 1, 2});
	REQUIRE(sel.Dropped == 0);
}

TEST_CASE("Shadow slots: budget 1 shadows only the primary and counts the rest as dropped", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, true}, {true, true}, {true, false}};
	const auto sel = SelectShadowDirectionals(lights, 1, 3);
	REQUIRE(sel.Slots == std::vector<int>{0, -1, -1});
	REQUIRE(sel.Dropped == 1);
}

TEST_CASE("Shadow slots: disabled and non-casting lights take no slot", "[lighting][shadows]")
{
	// A disabled, B (primary) enabled, C non-casting, D casting: B -> 0, D -> 1.
	const std::vector<DirectionalShadowCandidate> lights{{false, true}, {true, true}, {true, false}, {true, true}};
	const auto sel = SelectShadowDirectionals(lights, 4, 3);
	REQUIRE(sel.Slots == std::vector<int>{-1, 0, -1, 1});
	REQUIRE(sel.Dropped == 0);
}

TEST_CASE("Shadow slots: a non-casting primary still reserves slot 0", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, false}, {true, true}, {true, true}};
	const auto sel = SelectShadowDirectionals(lights, 2, 3);
	REQUIRE(sel.Slots == std::vector<int>{-1, 1, -1});
	REQUIRE(sel.Dropped == 1);
}

TEST_CASE("Shadow slots: secondary tiles never exceed the atlas capacity", "[lighting][shadows]")
{
	const std::vector<DirectionalShadowCandidate> lights{{true, true}, {true, true}, {true, true}};
	const auto sel = SelectShadowDirectionals(lights, 4, 1);
	REQUIRE(sel.Slots == std::vector<int>{0, 1, -1});
	REQUIRE(sel.Dropped == 1);
}
