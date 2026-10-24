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

TEST_CASE("PollLitSwap is idle until a swap begins", "[shaders]")
{
	LitSwapState state;
	CHECK(PollLitSwap(state, 3, false) == LitSwapPhase::Idle);
	CHECK(PollLitSwap(state, 3, true) == LitSwapPhase::Idle);
}

TEST_CASE("PollLitSwap stays pending while a compile is queued or running", "[shaders]")
{
	LitSwapState state;
	BeginLitSwap(state, 4);
	CHECK(PollLitSwap(state, 4, true) == LitSwapPhase::Pending);
	CHECK(PollLitSwap(state, 4, true) == LitSwapPhase::Pending);
	CHECK(state.Active);
}

TEST_CASE("PollLitSwap succeeds once the version advanced and nothing is compiling", "[shaders]")
{
	LitSwapState state;
	BeginLitSwap(state, 4);
	CHECK(PollLitSwap(state, 5, false) == LitSwapPhase::Succeeded);
	CHECK(PollLitSwap(state, 5, false) == LitSwapPhase::Idle);
}

TEST_CASE("PollLitSwap fails when the compile ended without publishing, and reports once", "[shaders]")
{
	LitSwapState state;
	BeginLitSwap(state, 4);
	CHECK(PollLitSwap(state, 4, false) == LitSwapPhase::Failed);
	CHECK(PollLitSwap(state, 4, false) == LitSwapPhase::Idle);
}

TEST_CASE("PollLitSwap: a request superseding a pending swap keeps the original start version", "[shaders]")
{
	LitSwapState state;
	BeginLitSwap(state, 4);
	// the first compile published v5 while the second request was already queued
	BeginLitSwap(state, 5);
	CHECK(PollLitSwap(state, 5, true) == LitSwapPhase::Pending);
	CHECK(PollLitSwap(state, 6, false) == LitSwapPhase::Succeeded);
}

TEST_CASE("PollLitSwap: a swap after a resolved one starts from the new version", "[shaders]")
{
	LitSwapState state;
	BeginLitSwap(state, 1);
	CHECK(PollLitSwap(state, 2, false) == LitSwapPhase::Succeeded);
	BeginLitSwap(state, 2);
	CHECK(PollLitSwap(state, 2, false) == LitSwapPhase::Failed);
}
