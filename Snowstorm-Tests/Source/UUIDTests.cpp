#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/UUID.hpp"

using namespace Snowstorm;

TEST_CASE("default-constructed UUIDs are non-zero and distinct", "[uuid]")
{
	const UUID a;
	const UUID b;
	REQUIRE(a.Value() != 0);
	REQUIRE(a != b); // collision is astronomically unlikely
}

TEST_CASE("UUID constructed from a value round-trips", "[uuid]")
{
	const UUID a(123456789ull);
	REQUIRE(a.Value() == 123456789ull);
	REQUIRE(static_cast<UUID::UnderlyingType>(a) == 123456789ull);
}

TEST_CASE("UUID survives a string round-trip", "[uuid]")
{
	const UUID a;
	REQUIRE(UUID::FromString(a.ToString()) == a);
}
