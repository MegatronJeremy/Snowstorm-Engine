#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/World/SimulationStateSingleton.hpp"

using Snowstorm::SimulationStateSingleton;
using Mode = SimulationStateSingleton::Mode;

TEST_CASE("SimulationState: Edit is neither playing nor possessing", "[simulation]")
{
	SimulationStateSingleton s;
	REQUIRE(s.Current == Mode::Edit);
	REQUIRE_FALSE(s.IsPlaying());
	REQUIRE_FALSE(s.IsPossessing());
}

TEST_CASE("SimulationState: Play simulates without possessing", "[simulation]")
{
	SimulationStateSingleton s;
	s.Current = Mode::Play;
	REQUIRE(s.IsPlaying());
	REQUIRE_FALSE(s.IsPossessing());
}

TEST_CASE("SimulationState: PlayGame is playing and possessing", "[simulation]")
{
	SimulationStateSingleton s;
	s.Current = Mode::PlayGame;
	REQUIRE(s.IsPlaying());
	REQUIRE(s.IsPossessing());
}
