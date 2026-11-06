#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/CascadeMath.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Render/ShadowAtlasMath.hpp"

#include <set>
#include <utility>

using namespace Snowstorm;

TEST_CASE("Atlas tiles are row-major squares", "[shadows][atlas]")
{
	const ShadowAtlasTile t0 = ShadowAtlasTileRect(0, 2, 1024);
	REQUIRE((t0.X == 0 && t0.Y == 0 && t0.Size == 1024));
	const ShadowAtlasTile t1 = ShadowAtlasTileRect(1, 2, 1024);
	REQUIRE((t1.X == 1024 && t1.Y == 0));
	const ShadowAtlasTile t2 = ShadowAtlasTileRect(2, 2, 1024);
	REQUIRE((t2.X == 0 && t2.Y == 1024));
	const ShadowAtlasTile t3 = ShadowAtlasTileRect(3, 2, 1024);
	REQUIRE((t3.X == 1024 && t3.Y == 1024 && t3.Size == 1024));
}

TEST_CASE("Cascade atlas tiles agree with the generic tile rect", "[shadows][atlas]")
{
	for (uint32_t i = 0; i < CascadeMath::kCascadeAtlasCols * CascadeMath::kCascadeAtlasCols; ++i)
	{
		const CascadeMath::TileRect c = CascadeMath::AtlasTile(i, 512);
		const ShadowAtlasTile g = ShadowAtlasTileRect(i, CascadeMath::kCascadeAtlasCols, 512);
		REQUIRE((c.X == g.X && c.Y == g.Y && c.Size == g.Size));
	}
}

TEST_CASE("A directional's ShadowSlot s maps to tile s - 1", "[shadows][atlas]")
{
	REQUIRE(DirectionalAtlasTile(1) == 0);
	REQUIRE(DirectionalAtlasTile(3) == 2);
	const ShadowAtlasTile t = ShadowAtlasTileRect(DirectionalAtlasTile(2), ShadowPass::kDirAtlasCols, 256);
	REQUIRE((t.X == 256 && t.Y == 0));
}

TEST_CASE("Point faces take consecutive tiles slot * 6 + face", "[shadows][atlas]")
{
	REQUIRE(PointAtlasTile(0, 0) == 0);
	REQUIRE(PointAtlasTile(0, 5) == 5);
	REQUIRE(PointAtlasTile(1, 0) == 6);
	REQUIRE(PointAtlasTile(1, 5) == 11);
}

TEST_CASE("Every point face of every shadowed point lands in a distinct in-bounds tile", "[shadows][atlas]")
{
	constexpr uint32_t tilePx = 128;
	constexpr uint32_t atlasPx = ShadowPass::kPointAtlasCols * tilePx;
	std::set<std::pair<uint32_t, uint32_t>> seen;
	for (int slot = 0; slot < 2; ++slot)
	{
		for (int face = 0; face < 6; ++face)
		{
			const ShadowAtlasTile t = ShadowAtlasTileRect(PointAtlasTile(slot, face), ShadowPass::kPointAtlasCols, tilePx);
			REQUIRE(t.X + t.Size <= atlasPx);
			REQUIRE(t.Y + t.Size <= atlasPx);
			REQUIRE(seen.emplace(t.X, t.Y).second);
		}
	}
	REQUIRE(seen.size() == 12);
}

TEST_CASE("Spot tiles fill the atlas without overlap", "[shadows][atlas]")
{
	std::set<std::pair<uint32_t, uint32_t>> seen;
	for (int i = 0; i < ShadowPass::kMaxShadowSpots; ++i)
	{
		const ShadowAtlasTile t = ShadowAtlasTileRect(static_cast<uint32_t>(i), ShadowPass::kSpotAtlasCols, 64);
		REQUIRE(t.X < ShadowPass::kSpotAtlasCols * 64);
		REQUIRE(t.Y < ShadowPass::kSpotAtlasCols * 64);
		REQUIRE(seen.emplace(t.X, t.Y).second);
	}
}
