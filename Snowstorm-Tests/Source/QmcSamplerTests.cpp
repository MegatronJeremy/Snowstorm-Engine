#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "Snowstorm/Render/QmcSampler.hpp"

using namespace Snowstorm;
using Catch::Approx;

namespace
{
	constexpr uint32_t kSeeds[] = {0u, 1u, 0xdeadbeefu, 12345u};
}

TEST_CASE("qmc: ReverseBits is an involution", "[qmc]")
{
	for (uint32_t x : {0u, 1u, 0x80000000u, 0x12345678u, 0xffffffffu})
	{
		REQUIRE(QmcSampler::ReverseBits(QmcSampler::ReverseBits(x)) == x);
	}
	REQUIRE(QmcSampler::ReverseBits(1u) == 0x80000000u);
}

TEST_CASE("qmc: nested scramble maps an aligned index block onto one aligned block", "[qmc]")
{
	for (uint32_t seed : kSeeds)
	{
		for (uint32_t k = 1; k <= 10; ++k)
		{
			std::vector<bool> seen(1u << k, false);
			const uint32_t block = QmcSampler::NestedUniformScramble(0u, seed) >> k;
			for (uint32_t i = 0; i < (1u << k); ++i)
			{
				const uint32_t s = QmcSampler::NestedUniformScramble(i, seed);
				REQUIRE((s >> k) == block);
				const uint32_t low = s & ((1u << k) - 1u);
				REQUIRE_FALSE(seen[low]);
				seen[low] = true;
			}
		}
	}
}

TEST_CASE("qmc: first 2^k points stratify each axis exactly", "[qmc]")
{
	for (uint32_t seed : kSeeds)
	{
		for (uint32_t k = 1; k <= 10; ++k)
		{
			const uint32_t n = 1u << k;
			std::vector<int> cx(n, 0), cy(n, 0);
			for (uint32_t i = 0; i < n; ++i)
			{
				const auto p = QmcSampler::Sample2D(i, 7u, seed);
				REQUIRE(p.X >= 0.0f);
				REQUIRE(p.X < 1.0f);
				REQUIRE(p.Y < 1.0f);
				++cx[static_cast<uint32_t>(p.X * static_cast<float>(n))];
				++cy[static_cast<uint32_t>(p.Y * static_cast<float>(n))];
			}
			for (uint32_t s = 0; s < n; ++s)
			{
				REQUIRE(cx[s] == 1);
				REQUIRE(cy[s] == 1);
			}
		}
	}
}

TEST_CASE("qmc: first 2^k points form a (0,2)-net on every 2^a x 2^b box", "[qmc]")
{
	for (uint32_t seed : kSeeds)
	{
		for (uint32_t k = 2; k <= 8; ++k)
		{
			const uint32_t n = 1u << k;
			for (uint32_t a = 0; a <= k; ++a)
			{
				const uint32_t b = k - a;
				const uint32_t nx = 1u << a, ny = 1u << b;
				std::vector<int> cells(n, 0);
				for (uint32_t i = 0; i < n; ++i)
				{
					const auto p = QmcSampler::Sample2D(i, 3u, seed);
					const uint32_t cx = static_cast<uint32_t>(p.X * static_cast<float>(nx));
					const uint32_t cy = static_cast<uint32_t>(p.Y * static_cast<float>(ny));
					++cells[cy * nx + cx];
				}
				for (int c : cells)
				{
					REQUIRE(c == 1);
				}
			}
		}
	}
}

TEST_CASE("qmc: deterministic in (index, pair, seed), distinct across pairs and seeds", "[qmc]")
{
	const auto a = QmcSampler::Sample2D(5u, 1u, 99u);
	const auto b = QmcSampler::Sample2D(5u, 1u, 99u);
	REQUIRE(a.X == b.X);
	REQUIRE(a.Y == b.Y);
	const auto otherPair = QmcSampler::Sample2D(5u, 2u, 99u);
	const auto otherSeed = QmcSampler::Sample2D(5u, 1u, 100u);
	REQUIRE((otherPair.X != a.X || otherPair.Y != a.Y));
	REQUIRE((otherSeed.X != a.X || otherSeed.Y != a.Y));
}

TEST_CASE("qmc: mean of 4096 points is 0.5 per dimension", "[qmc]")
{
	double sx = 0.0, sy = 0.0;
	for (uint32_t i = 0; i < 4096; ++i)
	{
		const auto p = QmcSampler::Sample2D(i, 11u, 42u);
		sx += p.X;
		sy += p.Y;
	}
	REQUIRE(sx / 4096.0 == Approx(0.5).margin(1e-3));
	REQUIRE(sy / 4096.0 == Approx(0.5).margin(1e-3));
}

TEST_CASE("qmc: pairs of one decision stay uncorrelated across pixels", "[qmc]")
{
	// Same index, different pixel seeds: the scramble must decorrelate, not repeat the point.
	int same = 0;
	for (uint32_t px = 0; px < 256; ++px)
	{
		const auto p = QmcSampler::Sample2D(0u, 0u, QmcSampler::Hash(px));
		const auto q = QmcSampler::Sample2D(0u, 0u, QmcSampler::Hash(px + 1000u));
		same += (p.X == q.X && p.Y == q.Y);
	}
	REQUIRE(same == 0);
}
