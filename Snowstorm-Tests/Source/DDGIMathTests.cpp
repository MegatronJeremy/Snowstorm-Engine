#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <utility>

#include "Snowstorm/Render/DDGIMath.hpp"

using namespace Snowstorm::DDGI;
using Catch::Approx;

TEST_CASE("ddgi: the fitted volume covers the bounds within the probe budget", "[render][ddgi]")
{
	const glm::vec3 lo(-15.0f, 0.0f, -6.0f);
	const glm::vec3 hi(15.0f, 13.0f, 6.0f);

	const Volume v = FitVolume(lo, hi, 2.0f, 4096);
	REQUIRE(v.Spacing == 2.0f);
	REQUIRE(v.Counts == glm::uvec3(16, 8, 7));
	const glm::vec3 last = ProbePosition(v, v.Counts - 1u);
	REQUIRE(glm::all(glm::lessThanEqual(v.Origin, lo)));
	REQUIRE(glm::all(glm::greaterThanEqual(last, hi)));
	REQUIRE((v.Origin + last).x * 0.5f == Approx((lo + hi).x * 0.5f));

	const Volume capped = FitVolume(lo, hi, 0.25f, 512);
	REQUIRE(ProbeCount(capped) <= 512);
	REQUIRE(capped.Spacing > 0.25f);
	REQUIRE(glm::all(glm::greaterThanEqual(ProbePosition(capped, capped.Counts - 1u), hi)));

	const Volume flat = FitVolume(glm::vec3(0.0f), glm::vec3(0.0f), 1.0f, 64);
	REQUIRE(flat.Counts == glm::uvec3(2u));
}

TEST_CASE("ddgi: probe index and grid coordinates round trip", "[render][ddgi]")
{
	Volume v;
	v.Counts = {5, 3, 4};
	for (uint32_t i = 0; i < ProbeCount(v); ++i)
	{
		REQUIRE(ProbeIndex(v, ProbeCoords(v, i)) == i);
	}
}

TEST_CASE("ddgi: every probe owns a distinct tile inside the atlas", "[render][ddgi]")
{
	Volume v;
	v.Counts = {16, 8, 7};
	for (const uint32_t interior : {kIrradianceTexels, kDistanceTexels})
	{
		const glm::uvec2 size = AtlasSize(v, interior);
		std::set<std::pair<uint32_t, uint32_t>> seen;
		for (uint32_t i = 0; i < ProbeCount(v); ++i)
		{
			const glm::uvec2 o = TileOrigin(v, i, interior);
			REQUIRE(o.x + interior + 2 <= size.x);
			REQUIRE(o.y + interior + 2 <= size.y);
			REQUIRE(seen.insert({o.x, o.y}).second);
		}
	}
	REQUIRE(AtlasSize(v, kDistanceTexels).x <= 16384);
}

TEST_CASE("ddgi: octahedral encode and decode round trip", "[render][ddgi]")
{
	for (uint32_t i = 0; i < 256; ++i)
	{
		const glm::vec3 n = SphericalFibonacci(i, 256);
		const glm::vec3 back = OctDecode(OctEncode(n));
		INFO("ray " << i);
		REQUIRE(glm::dot(n, back) == Approx(1.0f).margin(1e-5));
	}
}

namespace
{
	// Fold an octahedral coordinate that left [-1,1]^2 back across the edge it crossed.
	glm::vec2 Fold(glm::vec2 e)
	{
		if (std::abs(e.x) > 1.0f)
		{
			e = {std::copysign(2.0f, e.x) - e.x, -e.y};
		}
		if (std::abs(e.y) > 1.0f)
		{
			e = {-e.x, std::copysign(2.0f, e.y) - e.y};
		}
		return e;
	}
}

TEST_CASE("ddgi: a border texel copies the texel its octahedral position folds onto", "[render][ddgi]")
{
	for (const uint32_t n : {kIrradianceTexels, kDistanceTexels})
	{
		for (uint32_t y = 0; y < n + 2; ++y)
		{
			for (uint32_t x = 0; x < n + 2; ++x)
			{
				const bool border = x == 0 || y == 0 || x == n + 1 || y == n + 1;
				const glm::uvec2 src = BorderSource({x, y}, n);
				if (!border)
				{
					REQUIRE(src == glm::uvec2(x, y));
					continue;
				}
				INFO("n " << n << " border texel " << x << "," << y);
				REQUIRE(src.x >= 1);
				REQUIRE(src.x <= n);
				REQUIRE(src.y >= 1);
				REQUIRE(src.y <= n);
				const glm::vec2 outside = (glm::vec2(x, y) - 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
				const glm::vec2 folded = Fold(outside);
				const glm::vec2 source = TexelOct(src - 1u, n);
				REQUIRE(folded.x == Approx(source.x).margin(1e-5));
				REQUIRE(folded.y == Approx(source.y).margin(1e-5));
			}
		}
	}
}

TEST_CASE("ddgi: Fibonacci rays are unit length and balanced", "[render][ddgi]")
{
	glm::vec3 sum(0.0f);
	for (uint32_t i = 0; i < 128; ++i)
	{
		const glm::vec3 d = SphericalFibonacci(i, 128);
		REQUIRE(glm::length(d) == Approx(1.0f).margin(1e-5));
		sum += d;
	}
	REQUIRE(glm::length(sum / 128.0f) < 0.02f);
}

TEST_CASE("ddgi: the cell of a point gives its corner probe and trilinear weights", "[render][ddgi]")
{
	Volume v;
	v.Origin = {-1.0f, 0.0f, 2.0f};
	v.Spacing = 2.0f;
	v.Counts = {4, 3, 2};

	const Cell atProbe = CellAt(v, ProbePosition(v, {1, 1, 0}));
	REQUIRE(atProbe.Base == glm::uvec3(1, 1, 0));
	REQUIRE(atProbe.Alpha == glm::vec3(0.0f));

	const Cell mid = CellAt(v, ProbePosition(v, {2, 0, 0}) + glm::vec3(1.0f, 0.5f, 1.5f));
	REQUIRE(mid.Base == glm::uvec3(2, 0, 0));
	REQUIRE(mid.Alpha.x == Approx(0.5f));
	REQUIRE(mid.Alpha.y == Approx(0.25f));
	REQUIRE(mid.Alpha.z == Approx(0.75f));

	// Outside the volume the nearest cell is used, so the last probe on an axis is never a base.
	const Cell beyond = CellAt(v, glm::vec3(100.0f, -100.0f, 100.0f));
	REQUIRE(beyond.Base == glm::uvec3(2, 0, 0));
	REQUIRE(beyond.Alpha == glm::vec3(1.0f, 0.0f, 1.0f));
}

TEST_CASE("ddgi: Chebyshev visibility is 1 in front of the mean and falls off behind it", "[render][ddgi]")
{
	REQUIRE(ChebyshevWeight(3.0f, 9.5f, 2.0f) == 1.0f);
	REQUIRE(ChebyshevWeight(3.0f, 9.5f, 3.0f) == 1.0f);
	const float near = ChebyshevWeight(3.0f, 9.5f, 3.5f);
	const float far = ChebyshevWeight(3.0f, 9.5f, 6.0f);
	REQUIRE(near < 1.0f);
	REQUIRE(far < near);
	REQUIRE(ChebyshevWeight(3.0f, 9.0f, 50.0f) == 0.05f);
}
