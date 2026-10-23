#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Systems/RuntimeInitRule.hpp"

using namespace Snowstorm;

TEST_CASE("RuntimeInitRule: viewport size guard", "[runtimeinit]")
{
	CHECK(IsValidViewportSize(64u, 64u));
	CHECK_FALSE(IsValidViewportSize(63u, 64u));
	CHECK_FALSE(IsValidViewportSize(64u, 63u));
	CHECK_FALSE(IsValidViewportSize(0u, 0u));
	CHECK(IsValidViewportSize(1920u, 1080u));
}

TEST_CASE("RuntimeInitRule: UUID resolution", "[runtimeinit]")
{
	const auto a = static_cast<entt::entity>(3);
	const auto b = static_cast<entt::entity>(7);

	UuidEntityMap map;
	RegisterUuid(map, 11, a);
	RegisterUuid(map, 22, b);

	SECTION("unset UUID resolves to null")
	{
		CHECK(ResolveUuid(map, 0) == entt::null);
	}
	SECTION("known UUID resolves to its entity")
	{
		CHECK(ResolveUuid(map, 11) == a);
		CHECK(ResolveUuid(map, 22) == b);
	}
	SECTION("unknown UUID resolves to null")
	{
		CHECK(ResolveUuid(map, 33) == entt::null);
	}
	SECTION("UUID 0 is never registered")
	{
		RegisterUuid(map, 0, a);
		CHECK(map.size() == 2);
		CHECK(ResolveUuid(map, 0) == entt::null);
	}
	SECTION("a duplicate UUID keeps the entity registered last")
	{
		RegisterUuid(map, 11, b);
		CHECK(map.size() == 2);
		CHECK(ResolveUuid(map, 11) == b);
	}
	SECTION("an empty map resolves everything to null")
	{
		CHECK(ResolveUuid(UuidEntityMap{}, 11) == entt::null);
	}
}
