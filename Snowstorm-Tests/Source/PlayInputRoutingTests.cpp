#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Input/PlayInputRouting.hpp"

using namespace Snowstorm;

TEST_CASE("PlayInputRouting: outside possessed play nothing is captured", "[input]")
{
	const PlayInputStep s = StepPlayInput(false, true, {.Escape = true, .ViewportClicked = true});
	REQUIRE_FALSE(s.Captured);
	REQUIRE_FALSE(s.Stop);
}

TEST_CASE("PlayInputRouting: a captured session keeps input until released", "[input]")
{
	REQUIRE(StepPlayInput(true, true, {}).Captured);
	REQUIRE(StepPlayInput(true, true, {.ViewportClicked = true}).Captured);
}

TEST_CASE("PlayInputRouting: Escape or Shift+F1 releases without stopping", "[input]")
{
	for (const PlayInputEvents ev : {PlayInputEvents{.Escape = true}, PlayInputEvents{.ReleaseChord = true}})
	{
		const PlayInputStep s = StepPlayInput(true, true, ev);
		REQUIRE_FALSE(s.Captured);
		REQUIRE_FALSE(s.Stop);
	}
}

TEST_CASE("PlayInputRouting: released session stops on Escape and recaptures on a viewport click", "[input]")
{
	const PlayInputStep stop = StepPlayInput(true, false, {.Escape = true});
	REQUIRE(stop.Stop);
	REQUIRE_FALSE(stop.Captured);

	const PlayInputStep click = StepPlayInput(true, false, {.ViewportClicked = true});
	REQUIRE(click.Captured);
	REQUIRE_FALSE(click.Stop);

	REQUIRE_FALSE(StepPlayInput(true, false, {}).Captured);
}

TEST_CASE("PlayInputRouting: a text field keeps its Escape and clicks", "[input]")
{
	const PlayInputStep held = StepPlayInput(true, true, {.Escape = true, .WantTextInput = true});
	REQUIRE(held.Captured);
	REQUIRE_FALSE(held.Stop);

	const PlayInputStep released = StepPlayInput(true, false, {.Escape = true, .ViewportClicked = true, .WantTextInput = true});
	REQUIRE_FALSE(released.Captured);
	REQUIRE_FALSE(released.Stop);
}

TEST_CASE("PlayInputRouting: editor keys are blocked while typing or while the game holds input", "[input]")
{
	REQUIRE_FALSE(EditorKeysBlocked(false, false));
	REQUIRE(EditorKeysBlocked(true, false));
	REQUIRE(EditorKeysBlocked(false, true));

	InputStateSingleton input;
	REQUIRE_FALSE(input.EditorKeysBlocked());
	input.GameCaptured = true;
	REQUIRE(input.EditorKeysBlocked());
}
