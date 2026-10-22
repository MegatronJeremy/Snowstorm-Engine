#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/MeshBoundsBuilder.hpp"

#include <cmath>
#include <limits>

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	Vertex At(const float x, const float y, const float z)
	{
		Vertex v{};
		v.Position = {x, y, z};
		return v;
	}

	std::vector<Vertex> CubeCorners(const glm::vec3 center, const float halfExtent)
	{
		std::vector<Vertex> out;
		for (const float sx : {-1.0f, 1.0f})
		{
			for (const float sy : {-1.0f, 1.0f})
			{
				for (const float sz : {-1.0f, 1.0f})
				{
					out.push_back(At(center.x + sx * halfExtent, center.y + sy * halfExtent, center.z + sz * halfExtent));
				}
			}
		}
		return out;
	}
}

TEST_CASE("Unit cube bounds: box is the corners, sphere circumscribes them", "[assets][bounds]")
{
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices(CubeCorners({0.0f, 0.0f, 0.0f}, 0.5f), b));

	REQUIRE(b.Box.Min == glm::vec3(-0.5f));
	REQUIRE(b.Box.Max == glm::vec3(0.5f));
	REQUIRE(b.Sphere.Center == glm::vec3(0.0f));
	// Half the space diagonal: sqrt(3) * 0.5.
	REQUIRE(b.Sphere.Radius == Approx(0.8660254f).margin(1e-6));
}

TEST_CASE("Offset mesh: the sphere centres on the box centre, not the origin", "[assets][bounds]")
{
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices(CubeCorners({10.0f, -4.0f, 2.0f}, 1.0f), b));

	REQUIRE(b.Box.Min == glm::vec3(9.0f, -5.0f, 1.0f));
	REQUIRE(b.Box.Max == glm::vec3(11.0f, -3.0f, 3.0f));
	REQUIRE(b.Sphere.Center == glm::vec3(10.0f, -4.0f, 2.0f));
	REQUIRE(b.Sphere.Radius == Approx(1.7320508f).margin(1e-6));
}

TEST_CASE("Box-centre sphere is not the minimal sphere", "[assets][bounds]")
{
	// Three points on a line: centre is the box midpoint (1.5), radius reaches the far one (1.5).
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices({At(0, 0, 0), At(0.5f, 0, 0), At(3, 0, 0)}, b));
	REQUIRE(b.Sphere.Center == glm::vec3(1.5f, 0.0f, 0.0f));
	REQUIRE(b.Sphere.Radius == Approx(1.5f));
}

TEST_CASE("Single vertex yields a degenerate box and a zero radius", "[assets][bounds]")
{
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices({At(1.0f, 2.0f, 3.0f)}, b));

	REQUIRE(b.Box.Min == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(b.Box.Max == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(b.Sphere.Center == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(b.Sphere.Radius == 0.0f);
}

TEST_CASE("Empty vertex list reports failure and leaves the output untouched", "[assets][bounds]")
{
	MeshBounds b;
	b.Box.Min = glm::vec3(7.0f);
	b.Box.Max = glm::vec3(8.0f);
	b.Sphere.Radius = 9.0f;

	REQUIRE_FALSE(ComputeMeshBoundsFromVertices({}, b));
	REQUIRE(b.Box.Min == glm::vec3(7.0f));
	REQUIRE(b.Box.Max == glm::vec3(8.0f));
	REQUIRE(b.Sphere.Radius == 9.0f);
}

TEST_CASE("A NaN position is skipped by the box and the radius (current behaviour)", "[assets][bounds]")
{
	// glm::min/max are (y < x) ? y : x and (x < y) ? y : x, so every comparison with NaN is false and the
	// running extreme is kept; likewise std::max(r2, NaN) keeps r2. Pinned so a change to that is deliberate.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices({At(-1, -1, -1), At(nan, nan, nan), At(1, 1, 1)}, b));

	REQUIRE(b.Box.Min == glm::vec3(-1.0f));
	REQUIRE(b.Box.Max == glm::vec3(1.0f));
	REQUIRE(b.Sphere.Center == glm::vec3(0.0f));
	REQUIRE(b.Sphere.Radius == Approx(1.7320508f).margin(1e-6));
}

TEST_CASE("An infinite position reaches the box unclamped (current behaviour)", "[assets][bounds]")
{
	const float inf = std::numeric_limits<float>::infinity();
	MeshBounds b;
	REQUIRE(ComputeMeshBoundsFromVertices({At(0, 0, 0), At(inf, 0, 0)}, b));

	REQUIRE(b.Box.Min == glm::vec3(0.0f));
	REQUIRE(b.Box.Max.x == inf);
	REQUIRE(std::isinf(b.Sphere.Center.x));
}
