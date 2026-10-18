#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Lighting/PrimaryDirectional.hpp"

#include <vector>

#include <glm/gtc/matrix_transform.hpp>

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

TEST_CASE("Rotated light direction applies the entity rotation to the authored direction", "[lighting]")
{
	constexpr float eps = 1e-5f;
	const glm::vec3 authored(-1.0f, 0.0f, 0.0f);

	const glm::vec3 same = RotatedLightDirection(glm::mat3(1.0f), authored);
	REQUIRE(same.x == Catch::Approx(-1.0f).margin(eps));
	REQUIRE(same.y == Catch::Approx(0.0f).margin(eps));

	const glm::mat3 quarterZ(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)));
	const glm::vec3 turned = RotatedLightDirection(quarterZ, authored);
	REQUIRE(turned.x == Catch::Approx(0.0f).margin(eps));
	REQUIRE(turned.y == Catch::Approx(-1.0f).margin(eps));
	REQUIRE(turned.z == Catch::Approx(0.0f).margin(eps));

	const glm::vec3 unnormalized = RotatedLightDirection(quarterZ, glm::vec3(-3.0f, 0.0f, 0.0f));
	REQUIRE(glm::length(unnormalized) == Catch::Approx(1.0f).margin(eps));
	REQUIRE(unnormalized.y == Catch::Approx(-1.0f).margin(eps));
}
