#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/QueueFamilySelect.hpp"

using namespace Snowstorm;

namespace
{
	constexpr QueueFamilyCaps Gfx{true, true, true};
	constexpr QueueFamilyCaps ComputeOnly{false, true, true};
	constexpr QueueFamilyCaps TransferOnly{false, false, true};
}

TEST_CASE("Single universal family aliases compute and transfer to graphics", "[render][queue]")
{
	const auto s = SelectQueueFamilies({Gfx}, 0);
	CHECK(s.Compute == 0);
	CHECK(s.Transfer == 0);
	CHECK_FALSE(s.DedicatedCompute());
	CHECK_FALSE(s.DedicatedTransfer());
}

TEST_CASE("Compute-without-graphics family is chosen, first one wins", "[render][queue]")
{
	const auto s = SelectQueueFamilies({Gfx, TransferOnly, ComputeOnly, ComputeOnly}, 0);
	CHECK(s.Compute == 2);
	CHECK(s.Transfer == 1);
	CHECK(s.DedicatedCompute());
	CHECK(s.DedicatedTransfer());
}

TEST_CASE("A second graphics-capable family is never an async compute candidate", "[render][queue]")
{
	const auto s = SelectQueueFamilies({Gfx, Gfx}, 0);
	CHECK_FALSE(s.DedicatedCompute());
}

TEST_CASE("Graphics family index other than zero is the alias", "[render][queue]")
{
	const auto s = SelectQueueFamilies({ComputeOnly, Gfx}, 1);
	CHECK(s.Compute == 0);
	CHECK(s.Graphics == 1);
}
