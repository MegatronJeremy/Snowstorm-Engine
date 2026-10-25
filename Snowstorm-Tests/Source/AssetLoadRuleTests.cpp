#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetLoadRule.hpp"

#include <string>

using namespace Snowstorm;
namespace R = Snowstorm::AssetLoadRule;

TEST_CASE("AssetLoadRule finalize budget", "[assets][load]")
{
	SECTION("an empty batch finalizes nothing")
	{
		CHECK(R::FinalizeCount(0, R::kMaxMeshFinalizePerFrame) == 0);
	}
	SECTION("a batch under the budget finalizes whole")
	{
		CHECK(R::FinalizeCount(3, R::kMaxMeshFinalizePerFrame) == 3);
		CHECK(R::FinalizeCount(R::kMaxTextureFinalizePerFrame, R::kMaxTextureFinalizePerFrame) == R::kMaxTextureFinalizePerFrame);
	}
	SECTION("a batch over the budget is capped, the rest re-queues")
	{
		CHECK(R::FinalizeCount(25, R::kMaxMeshFinalizePerFrame) == 6);
		CHECK(R::FinalizeCount(69, R::kMaxTextureFinalizePerFrame) == 4);
	}
	SECTION("budgets are pinned")
	{
		CHECK(R::kMaxMeshFinalizePerFrame == 6);
		CHECK(R::kMaxTextureFinalizePerFrame == 4);
	}
	SECTION("a batch drains in ceil(n / budget) frames")
	{
		size_t left = 25;
		int frames = 0;
		while (left > 0)
		{
			left -= R::FinalizeCount(left, R::kMaxMeshFinalizePerFrame);
			++frames;
		}
		CHECK(frames == 5);
	}
}

TEST_CASE("AssetLoadRule completed entries", "[assets][load]")
{
	CHECK(R::ShouldFinalize(true));
	CHECK_FALSE(R::ShouldFinalize(false));
}

TEST_CASE("AssetLoadRule pending count and progress reset", "[assets][load]")
{
	SECTION("meshes and textures both count")
	{
		CHECK(R::PendingCount(0, 0) == 0);
		CHECK(R::PendingCount(2, 0) == 2);
		CHECK(R::PendingCount(0, 5) == 5);
		CHECK(R::PendingCount(3, 4) == 7);
	}
	SECTION("the high-water mark resets only when both kinds are idle")
	{
		CHECK(R::ShouldResetPendingTotal(0, 0));
		CHECK_FALSE(R::ShouldResetPendingTotal(1, 0));
		CHECK_FALSE(R::ShouldResetPendingTotal(0, 1));
		CHECK_FALSE(R::ShouldResetPendingTotal(2, 3));
	}
	SECTION("reset agrees with a zero pending count")
	{
		for (size_t m = 0; m < 3; ++m)
			for (size_t t = 0; t < 3; ++t)
				CHECK(R::ShouldResetPendingTotal(m, t) == (R::PendingCount(m, t) == 0));
	}
}

TEST_CASE("AssetLoadRule unsettled stage and slot residency", "[assets][load]")
{
	CHECK(std::string(R::StageName(true)) == "decoded, awaiting GPU upload");
	CHECK(std::string(R::StageName(false)) == "on a worker");

	CHECK(R::IsSlotResident(0, true));
	CHECK(R::IsSlotResident(0, false));
	CHECK(R::IsSlotResident(7, false));
	CHECK_FALSE(R::IsSlotResident(7, true));
}
