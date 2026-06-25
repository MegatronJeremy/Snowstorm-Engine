#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Math/Picking.hpp"

using namespace Snowstorm;

namespace
{
	// Camera at +Z looking toward the origin (down -Z), RH, zero-to-one depth — matches
	// CameraRuntimeUpdateSystem's glm::perspectiveRH_ZO usage.
	glm::mat4 MakeViewProjection()
	{
		const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
		const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f, 0.0f, 5.0f),
		                                     glm::vec3(0.0f, 0.0f, 0.0f),
		                                     glm::vec3(0.0f, 1.0f, 0.0f));
		return proj * view;
	}
}

TEST_CASE("ray-AABB: ray straight into a box hits", "[picking]")
{
	const Ray r{glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
	const AABB box{glm::vec3(-1.0f), glm::vec3(1.0f)};

	const auto hit = RayIntersectsAABB(r, box);
	REQUIRE(hit.has_value());
	REQUIRE(*hit == Catch::Approx(4.0f)); // enters the box at z=1, i.e. 4 units from origin
}

TEST_CASE("ray-AABB: ray pointing away misses", "[picking]")
{
	const Ray r{glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
	const AABB box{glm::vec3(-1.0f), glm::vec3(1.0f)};
	REQUIRE_FALSE(RayIntersectsAABB(r, box).has_value());
}

TEST_CASE("ray-AABB: parallel ray outside the slab misses", "[picking]")
{
	const Ray r{glm::vec3(5.0f, 10.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
	const AABB box{glm::vec3(-1.0f), glm::vec3(1.0f)};
	REQUIRE_FALSE(RayIntersectsAABB(r, box).has_value());
}

TEST_CASE("ray-AABB: origin inside the box returns 0", "[picking]")
{
	const Ray r{glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
	const AABB box{glm::vec3(-1.0f), glm::vec3(1.0f)};
	const auto hit = RayIntersectsAABB(r, box);
	REQUIRE(hit.has_value());
	REQUIRE(*hit == Catch::Approx(0.0f));
}

TEST_CASE("screen-to-ray: center pixel aims at the look target", "[picking]")
{
	const glm::mat4 vp = MakeViewProjection();
	// Center of a 800x600 viewport.
	const Ray r = ScreenPointToRay(400.0f, 300.0f, 800.0f, 600.0f, vp);

	// Camera sits at +Z looking down -Z, so the center ray should point roughly along -Z.
	REQUIRE(r.Direction.z < -0.9f);
	REQUIRE(glm::abs(r.Direction.x) < 0.05f);
	REQUIRE(glm::abs(r.Direction.y) < 0.05f);

	// And it should hit a unit box at the origin.
	const AABB box{glm::vec3(-1.0f), glm::vec3(1.0f)};
	REQUIRE(RayIntersectsAABB(r, box).has_value());
}

TEST_CASE("screen-to-ray: corner pixel misses a small centered box", "[picking]")
{
	const glm::mat4 vp = MakeViewProjection();
	// Top-left corner of the viewport: ray should veer off and miss a tiny centered box.
	const Ray r = ScreenPointToRay(0.0f, 0.0f, 800.0f, 600.0f, vp);
	const AABB smallBox{glm::vec3(-0.2f), glm::vec3(0.2f)};
	REQUIRE_FALSE(RayIntersectsAABB(r, smallBox).has_value());
}
