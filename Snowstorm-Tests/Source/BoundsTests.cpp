#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Math/Bounds.hpp"

using namespace Snowstorm;
using Catch::Approx;

TEST_CASE("AABB reports center and extents", "[math]")
{
	const AABB b{glm::vec3(-1.0f), glm::vec3(1.0f)};
	REQUIRE(b.Center().x == Approx(0.0f));
	REQUIRE(b.Center().y == Approx(0.0f));
	REQUIRE(b.Extents().x == Approx(1.0f));
}

TEST_CASE("TransformAABB by identity leaves the box unchanged", "[math]")
{
	const AABB b{glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f)};
	const AABB t = TransformAABB(b, glm::mat4(1.0f));
	REQUIRE(t.Min.x == Approx(b.Min.x));
	REQUIRE(t.Min.z == Approx(b.Min.z));
	REQUIRE(t.Max.y == Approx(b.Max.y));
}

TEST_CASE("TransformAABB by a translation shifts the center", "[math]")
{
	const AABB b{glm::vec3(-1.0f), glm::vec3(1.0f)};
	const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
	const AABB t = TransformAABB(b, m);
	REQUIRE(t.Center().x == Approx(10.0f));
	REQUIRE(t.Extents().x == Approx(1.0f)); // size preserved under pure translation
}
