#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "Snowstorm/Math/Frustum.hpp"

using namespace Snowstorm;

namespace
{
	// Camera at the origin looking down -Z (right-handed), zero-to-one depth — matches
	// CameraRuntimeUpdateSystem's glm::perspectiveRH_ZO usage.
	Frustum MakeFrustum(const float nearZ = 1.0f, const float farZ = 100.0f)
	{
		const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, nearZ, farZ);
		const glm::mat4 view = glm::mat4(1.0f);
		return Frustum::FromViewProjection(proj * view);
	}
}

TEST_CASE("a point in front within range is visible", "[frustum]")
{
	const Frustum f = MakeFrustum();
	REQUIRE(f.IntersectsSphere(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f));
	REQUIRE(f.IntersectsAABB(AABB{glm::vec3(-1.0f, -1.0f, -11.0f), glm::vec3(1.0f, 1.0f, -9.0f)}));
}

TEST_CASE("a point behind the camera is culled", "[frustum]")
{
	const Frustum f = MakeFrustum();
	REQUIRE_FALSE(f.IntersectsSphere(glm::vec3(0.0f, 0.0f, 10.0f), 0.1f));
}

// Regression for the near-plane clip-space convention (RH_ZO). With the old OpenGL
// [-1,1] extraction the near plane sat at the wrong depth and this point leaked through.
TEST_CASE("a point nearer than the near plane is culled", "[frustum]")
{
	const Frustum f = MakeFrustum(1.0f, 100.0f); // near plane at 1 unit
	REQUIRE_FALSE(f.IntersectsSphere(glm::vec3(0.0f, 0.0f, -0.7f), 0.05f));
}

TEST_CASE("a point beyond the far plane is culled", "[frustum]")
{
	const Frustum f = MakeFrustum(1.0f, 100.0f);
	REQUIRE_FALSE(f.IntersectsSphere(glm::vec3(0.0f, 0.0f, -200.0f), 1.0f));
}
