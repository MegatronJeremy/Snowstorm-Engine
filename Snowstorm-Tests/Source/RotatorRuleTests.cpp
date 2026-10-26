#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/RotatorMath.hpp"

#include <cstdint>

using namespace Snowstorm;

TEST_CASE("RotatorTickDue advances once per clock value when fixed-step or route-driven", "[rotator]")
{
	constexpr uint64_t first = UINT64_MAX;
	CHECK(RotatorTickDue(true, false, 0, first));
	CHECK(RotatorTickDue(false, true, 0, first));
	CHECK_FALSE(RotatorTickDue(true, false, 7, 7));
	CHECK_FALSE(RotatorTickDue(false, true, 7, 7));
	CHECK(RotatorTickDue(true, false, 8, 7));
	// Free-running: every call advances, even on a repeated clock value.
	CHECK(RotatorTickDue(false, false, 7, 7));
}

TEST_CASE("RotatorSkipsEntity skips only the gizmo-held entity", "[rotator]")
{
	const auto a = static_cast<entt::entity>(1);
	const auto b = static_cast<entt::entity>(2);
	CHECK(RotatorSkipsEntity(a, a));
	CHECK_FALSE(RotatorSkipsEntity(a, b));
	CHECK_FALSE(RotatorSkipsEntity(a, entt::null));
	CHECK_FALSE(RotatorSkipsEntity(entt::null, entt::null));
}
