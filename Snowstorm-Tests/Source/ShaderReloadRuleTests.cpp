#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/ShaderReloadRule.hpp"

using namespace Snowstorm;

TEST_CASE("LitVariantNeedsCheck fires on the first ready frame whatever the wanted variant", "[shaders]")
{
	const LitVariantState fresh;
	CHECK(LitVariantNeedsCheck(fresh, false, false));
	CHECK(LitVariantNeedsCheck(fresh, true, false));
	CHECK(LitVariantNeedsCheck(fresh, true, true));
}

TEST_CASE("LitVariantNeedsCheck is quiet while the wanted variant is unchanged", "[shaders]")
{
	CHECK_FALSE(LitVariantNeedsCheck({true, false, false}, false, false));
	CHECK_FALSE(LitVariantNeedsCheck({true, true, false}, true, false));
	CHECK_FALSE(LitVariantNeedsCheck({true, true, true}, true, true));
}

TEST_CASE("LitVariantNeedsCheck fires when either axis flips", "[shaders]")
{
	CHECK(LitVariantNeedsCheck({true, false, false}, true, false));
	CHECK(LitVariantNeedsCheck({true, true, false}, false, false));
	CHECK(LitVariantNeedsCheck({true, true, false}, true, true));
	CHECK(LitVariantNeedsCheck({true, true, true}, true, false));
}

TEST_CASE("AdvanceReloadTimer fires strictly after one second and resets", "[shaders]")
{
	float since = 0.0f;
	CHECK_FALSE(AdvanceReloadTimer(since, 0.5f));
	CHECK(since == 0.5f);
	CHECK_FALSE(AdvanceReloadTimer(since, 0.5f)); // exactly the interval: not yet
	CHECK(since == 1.0f);
	CHECK(AdvanceReloadTimer(since, 0.25f));
	CHECK(since == 0.0f);
	CHECK_FALSE(AdvanceReloadTimer(since, 0.0f));
}

TEST_CASE("AdvanceReloadTimer drops the overshoot of a long frame", "[shaders]")
{
	float since = 0.0f;
	CHECK(AdvanceReloadTimer(since, 5.0f));
	CHECK(since == 0.0f);
	CHECK_FALSE(AdvanceReloadTimer(since, 0.9f));
}
