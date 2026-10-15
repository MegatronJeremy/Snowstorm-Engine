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
