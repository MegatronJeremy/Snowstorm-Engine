#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/LoadProgress.hpp"

using namespace Snowstorm;

TEST_CASE("LoadProgress is hidden with no recorded total", "[assets]")
{
	const LoadProgress p = MakeLoadProgress(0, 0);
	REQUIRE_FALSE(p.Visible());
	REQUIRE(p.Done == 0);
	REQUIRE(p.Total == 0);
}

TEST_CASE("LoadProgress counts done and fraction from pending over the high-water total", "[assets]")
{
	const LoadProgress p = MakeLoadProgress(3, 12);
	REQUIRE(p.Visible());
	REQUIRE(p.Done == 9);
	REQUIRE(p.Total == 12);
	REQUIRE(p.Fraction == Catch::Approx(0.75f));
}

TEST_CASE("LoadProgress starts at zero when everything is pending", "[assets]")
{
	const LoadProgress p = MakeLoadProgress(5, 5);
	REQUIRE(p.Visible());
	REQUIRE(p.Done == 0);
	REQUIRE(p.Fraction == Catch::Approx(0.0f));
}

TEST_CASE("LoadProgress is hidden once the queue is empty", "[assets]")
{
	const LoadProgress p = MakeLoadProgress(0, 7);
	REQUIRE_FALSE(p.Visible());
	REQUIRE(p.Done == 7);
	REQUIRE(p.Fraction == Catch::Approx(1.0f));
}

TEST_CASE("LoadProgress clamps pending above the total instead of wrapping", "[assets]")
{
	const LoadProgress p = MakeLoadProgress(9, 4);
	REQUIRE(p.Done == 0);
	REQUIRE(p.Total == 4);
	REQUIRE(p.Fraction == Catch::Approx(0.0f));
}
