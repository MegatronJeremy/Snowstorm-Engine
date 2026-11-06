#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/DenoiserTemporalRule.hpp"

using namespace Snowstorm;

TEST_CASE("Temporal off passes the raw trace through and clears history", "[denoiser]")
{
	const auto d = DecideDenoiserTemporal(false, true, true, 7);
	REQUIRE(d.PassThrough);
	REQUIRE_FALSE(d.NextHistoryValid);
}

TEST_CASE("A missing velocity buffer passes through and clears history", "[denoiser]")
{
	const auto d = DecideDenoiserTemporal(true, false, true, 7);
	REQUIRE(d.PassThrough);
	REQUIRE_FALSE(d.NextHistoryValid);
}

TEST_CASE("First temporal frame is invalid, the second is valid", "[denoiser]")
{
	bool flag = false;
	auto d = DecideDenoiserTemporal(true, true, flag, 0);
	REQUIRE_FALSE(d.PassThrough);
	REQUIRE_FALSE(d.HistoryValid);
	flag = d.NextHistoryValid;
	d = DecideDenoiserTemporal(true, true, flag, 1);
	REQUIRE(d.HistoryValid);
	REQUIRE(d.NextHistoryValid);
}

TEST_CASE("Re-enabling after off starts invalid", "[denoiser]")
{
	bool flag = DecideDenoiserTemporal(true, true, false, 0).NextHistoryValid;
	REQUIRE(flag);
	flag = DecideDenoiserTemporal(false, true, flag, 1).NextHistoryValid;
	REQUIRE_FALSE(flag);
	REQUIRE_FALSE(DecideDenoiserTemporal(true, true, flag, 2).HistoryValid);
}

TEST_CASE("History parity alternates with the frame counter", "[denoiser]")
{
	for (uint64_t f = 0; f < 6; ++f)
	{
		const auto d = DecideDenoiserTemporal(true, true, true, f);
		REQUIRE(d.CurIdx == (f & 1u));
		REQUIRE(d.PrevIdx == (d.CurIdx ^ 1u));
	}
	REQUIRE(DecideDenoiserTemporal(true, true, true, 0xFFFFFFFFFFFFFFFFull).CurIdx == 1u);
}
