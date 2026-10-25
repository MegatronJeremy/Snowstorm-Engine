#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Lighting/LightPacking.hpp"
#include "Snowstorm/Lighting/PrimaryDirectional.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Snowstorm;
using Catch::Approx;

TEST_CASE("Directional packing normalizes the direction and copies the rest", "[lighting][pack]")
{
	const GPUDirectionalLight l = PackDirectionalLight({0.0f, -3.0f, 0.0f}, 2.5f, {1.0f, 0.5f, 0.25f}, 0);
	REQUIRE(l.Direction == glm::vec3(0.0f, -1.0f, 0.0f));
	REQUIRE(l.Intensity == 2.5f);
	REQUIRE(l.Color == glm::vec3(1.0f, 0.5f, 0.25f));
	REQUIRE(l.ShadowSlot == 0);
}

TEST_CASE("Rotated sun direction: turned by the entity rotation, as authored without one", "[lighting][pack]")
{
	const glm::vec3 authored{0.0f, 0.0f, -1.0f};
	// No rotator: identity rotation keeps the authored direction.
	REQUIRE(RotatedLightDirection(glm::mat3(1.0f), authored) == glm::vec3(0.0f, 0.0f, -1.0f));
	// 90 degrees about Y turns -Z to -X.
	const glm::mat3 r(glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 1, 0)));
	const glm::vec3 d = RotatedLightDirection(r, authored);
	REQUIRE(d.x == Approx(-1.0f).margin(1e-6));
	REQUIRE(d.y == Approx(0.0f).margin(1e-6));
	REQUIRE(d.z == Approx(0.0f).margin(1e-6));
	// A scaled authored direction and a scaled transform still give a unit vector.
	const glm::mat3 scaled = r * 3.0f;
	REQUIRE(glm::length(RotatedLightDirection(scaled, authored * 5.0f)) == Approx(1.0f));
}

TEST_CASE("Point packing copies fields and zeroes the pad", "[lighting][pack]")
{
	const GPUPointLight p = PackPointLight({1, 2, 3}, 9.0f, {0.1f, 0.2f, 0.3f}, 4.0f, 1);
	REQUIRE(p.Position == glm::vec3(1, 2, 3));
	REQUIRE(p.Range == 9.0f);
	REQUIRE(p.Color == glm::vec3(0.1f, 0.2f, 0.3f));
	REQUIRE(p.Intensity == 4.0f);
	REQUIRE(p.ShadowSlot == 1);
	REQUIRE(p.ShadowPad == glm::vec3(0.0f));
}

TEST_CASE("Spot forward is the rotated -Z axis, unit length under scale", "[lighting][pack]")
{
	REQUIRE(SpotForward(glm::mat3(1.0f)) == glm::vec3(0.0f, 0.0f, -1.0f));
	const glm::mat3 r(glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 1, 0)));
	const glm::vec3 f = SpotForward(r * 2.0f);
	REQUIRE(f.x == Approx(-1.0f).margin(1e-6));
	REQUIRE(glm::length(f) == Approx(1.0f));
}

TEST_CASE("Spot cone is stored as cosines with outer clamped to inner", "[lighting][pack]")
{
	const glm::mat4 id(1.0f);
	const GPUSpotLight s = PackSpotLight({0, 1, 0}, 5.0f, {1, 1, 1}, 3.0f, {0, 0, -1}, 20.0f, 40.0f, 2, id, {0, 0, 0.5f, 0.5f});
	REQUIRE(s.CosInner == Approx(std::cos(glm::radians(20.0f))));
	REQUIRE(s.CosOuter == Approx(std::cos(glm::radians(40.0f))));
	REQUIRE(s.CosInner > s.CosOuter);
	REQUIRE(s.ShadowIndex == 2);
	REQUIRE(s.ShadowAtlasRect == glm::vec4(0, 0, 0.5f, 0.5f));
	REQUIRE(s.ShadowViewProj == id);
	REQUIRE(s.ShadowPad == glm::vec2(0.0f));

	// Outer below inner collapses to inner: the cone has no falloff band but the denominator stays positive.
	const GPUSpotLight c = PackSpotLight({0, 0, 0}, 1, {1, 1, 1}, 1, {0, 0, -1}, 30.0f, 10.0f, -1, id, {0, 0, 1, 1});
	REQUIRE(c.CosOuter == c.CosInner);
	REQUIRE(SpotOuterRadians(30.0f, 10.0f) == Approx(glm::radians(30.0f)));
}

TEST_CASE("Full arrays are reported at the cap and not before", "[lighting][pack]")
{
	LightDataBlock b;
	REQUIRE_FALSE(DirectionalFull(b));
	REQUIRE_FALSE(PointFull(b));
	REQUIRE_FALSE(SpotFull(b));
	b.LightCount = MAX_DIRECTIONAL_LIGHTS - 1;
	b.PointCount = MAX_POINT_LIGHTS - 1;
	b.SpotCount = MAX_SPOT_LIGHTS - 1;
	REQUIRE_FALSE(DirectionalFull(b));
	REQUIRE_FALSE(PointFull(b));
	REQUIRE_FALSE(SpotFull(b));
	b.LightCount = MAX_DIRECTIONAL_LIGHTS;
	b.PointCount = MAX_POINT_LIGHTS;
	b.SpotCount = MAX_SPOT_LIGHTS;
	REQUIRE(DirectionalFull(b));
	REQUIRE(PointFull(b));
	REQUIRE(SpotFull(b));
}
