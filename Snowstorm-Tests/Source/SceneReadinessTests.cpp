#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/SceneReadiness.hpp"

using namespace Snowstorm;

TEST_CASE("SceneReadiness is not ready on the first idle frame", "[assets]")
{
	SceneReadiness r;
	REQUIRE_FALSE(r.Ready());
	REQUIRE_FALSE(r.Update(0, 0));
	REQUIRE(r.Update(0, 0));
}

TEST_CASE("SceneReadiness drops back to loading when work appears", "[assets]")
{
	SceneReadiness r;
	r.Update(0, 0);
	REQUIRE(r.Update(0, 0));
	REQUIRE_FALSE(r.Update(3, 0));
	REQUIRE_FALSE(r.Update(0, 1));
	REQUIRE_FALSE(r.Update(0, 0));
	REQUIRE(r.Update(0, 0));
}

TEST_CASE("SceneReadiness Reset restarts the settle window", "[assets]")
{
	SceneReadiness r;
	r.Update(0, 0);
	r.Update(0, 0);
	r.Reset();
	REQUIRE_FALSE(r.Ready());
}
