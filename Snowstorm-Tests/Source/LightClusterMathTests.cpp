#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/LightClusterMath.hpp"

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	ClusterGrid MakeGrid()
	{
		ClusterGrid g;
		g.TilePx = 64;
		g.Width = 1915;
		g.Height = 1064;
		g.Slices = 24;
		g.Near = 0.1f;
		g.Far = 100.0f;
		return g;
	}
}

TEST_CASE("Grid dimensions round up to cover the target", "[render][clusters]")
{
	const ClusterGrid g = MakeGrid();
	CHECK(g.TilesX() == 30);
	CHECK(g.TilesY() == 17);
	CHECK(g.ClusterCount() == 30u * 17u * 24u);
	CHECK(g.Index(0, 0, 0) == 0);
	CHECK(g.Index(g.TilesX() - 1, g.TilesY() - 1, g.Slices - 1) == g.ClusterCount() - 1);
}

TEST_CASE("Slice index is monotone and clamps", "[render][clusters]")
{
	const ClusterGrid g = MakeGrid();
	CHECK(ClusterSliceFromDepth(g, 0.0f) == 0);
	CHECK(ClusterSliceFromDepth(g, g.Near) == 0);
	CHECK(ClusterSliceFromDepth(g, g.Far) == g.Slices - 1);
	CHECK(ClusterSliceFromDepth(g, g.Far * 10.0f) == g.Slices - 1);
	uint32_t prev = 0;
	for (float d = g.Near; d < g.Far; d *= 1.05f)
	{
		const uint32_t s = ClusterSliceFromDepth(g, d);
		CHECK(s >= prev);
		prev = s;
	}
}

TEST_CASE("Slice depth ranges abut and round-trip", "[render][clusters]")
{
	const ClusterGrid g = MakeGrid();
	CHECK(ClusterSliceDepth(g, 0) == Approx(g.Near));
	CHECK(ClusterSliceDepth(g, g.Slices) == Approx(g.Far));
	for (uint32_t k = 0; k < g.Slices; ++k)
	{
		const float lo = ClusterSliceDepth(g, k);
		const float hi = ClusterSliceDepth(g, k + 1);
		CHECK(hi > lo);
		CHECK(ClusterSliceFromDepth(g, lo * 1.001f) == k);
		CHECK(ClusterSliceFromDepth(g, hi * 0.999f) == k);
	}
}

TEST_CASE("Cluster bounds tile the screen and slices abut", "[render][clusters]")
{
	const ClusterGrid g = MakeGrid();
	const float tanHalf = 0.5f;
	const float aspect = static_cast<float>(g.Width) / static_cast<float>(g.Height);
	const float tx = tanHalf * aspect;

	const ClusterAabb first = ClusterBounds(g, 0, 0, 0, tanHalf, aspect);
	const ClusterAabb lastX = ClusterBounds(g, g.TilesX() - 1, 0, g.Slices - 1, tanHalf, aspect);
	const ClusterAabb lastY = ClusterBounds(g, 0, g.TilesY() - 1, g.Slices - 1, tanHalf, aspect);
	CHECK(first.Min[0] == Approx(-tx * first.Max[2]));
	CHECK(lastX.Max[0] == Approx(tx * lastX.Max[2]));
	CHECK(first.Max[1] == Approx(tanHalf * first.Max[2]));
	CHECK(lastY.Min[1] == Approx(-tanHalf * lastY.Max[2]));

	for (uint32_t k = 0; k + 1 < g.Slices; ++k)
	{
		const ClusterAabb a = ClusterBounds(g, 3, 4, k, tanHalf, aspect);
		const ClusterAabb b = ClusterBounds(g, 3, 4, k + 1, tanHalf, aspect);
		CHECK(a.Max[2] == Approx(b.Min[2]));
	}

	const ClusterAabb l = ClusterBounds(g, 4, 2, 5, tanHalf, aspect);
	const ClusterAabb r = ClusterBounds(g, 5, 2, 5, tanHalf, aspect);
	CHECK(r.Min[0] <= l.Max[0]);
	CHECK(r.Max[0] > l.Max[0]);
}

TEST_CASE("A projected point lies in the cluster its pixel and depth select", "[render][clusters]")
{
	const ClusterGrid g = MakeGrid();
	const float tanHalf = 0.6f;
	const float aspect = static_cast<float>(g.Width) / static_cast<float>(g.Height);
	for (uint32_t px : {0u, 100u, 957u, 1914u})
		for (uint32_t py : {0u, 63u, 64u, 700u, 1063u})
			for (float z : {0.15f, 1.0f, 12.5f, 99.0f})
			{
				const float ndcX = 2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(g.Width) - 1.0f;
				const float ndcY = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(g.Height);
				const float p[3] = {ndcX * tanHalf * aspect * z, ndcY * tanHalf * z, z};
				const ClusterAabb b = ClusterBounds(g, ClusterTileFromPixel(g, px), ClusterTileFromPixel(g, py), ClusterSliceFromDepth(g, z), tanHalf, aspect);
				for (int i = 0; i < 3; ++i)
				{
					CHECK(p[i] >= b.Min[i] - 1e-4f);
					CHECK(p[i] <= b.Max[i] + 1e-4f);
				}
			}
}

TEST_CASE("Sphere vs AABB edge cases", "[render][clusters]")
{
	const ClusterAabb b{{0, 0, 1}, {1, 1, 2}};
	CHECK(SphereOverlapsAabb({{0.5f, 0.5f, 1.5f}, 0.1f}, b));
	CHECK(SphereOverlapsAabb({{2.0f, 0.5f, 1.5f}, 1.0f}, b));
	CHECK_FALSE(SphereOverlapsAabb({{2.0f, 0.5f, 1.5f}, 0.99f}, b));
	CHECK_FALSE(SphereOverlapsAabb({{0.5f, 0.5f, -3.0f}, 1.0f}, b));
	CHECK(SphereOverlapsAabb({{0.5f, 0.5f, 0.5f}, 0.5f}, b));
	CHECK(SphereOverlapsAabb({{5.0f, 5.0f, 5.0f}, 100.0f}, b));
	CHECK_FALSE(SphereOverlapsAabb({{3.0f, 3.0f, 3.0f}, 1.0f}, b));
}

TEST_CASE("CPU cull keeps ascending order and drops overflow", "[render][clusters]")
{
	const ClusterAabb b{{0, 0, 1}, {1, 1, 2}};
	std::vector<ClusterSphere> lights;
	for (int i = 0; i < 6; ++i)
		lights.push_back({{0.5f, 0.5f, 1.5f}, 0.2f});
	lights.insert(lights.begin() + 2, ClusterSphere{{50.0f, 0.0f, 0.0f}, 1.0f});

	const auto all = CullClusterLights(b, lights, 32);
	CHECK(all == std::vector<uint32_t>{0, 1, 3, 4, 5, 6});
	const auto capped = CullClusterLights(b, lights, 3);
	CHECK(capped == std::vector<uint32_t>{0, 1, 3});
	CHECK(CullClusterLights(b, {}, 4).empty());
}
