#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/VelocityPolicy.hpp"

using namespace Snowstorm;

TEST_CASE("No consumer means no velocity route", "[render][velocity]")
{
	CHECK_FALSE(AnyVelocityConsumer({}));
	CHECK(ChooseVelocityRoute({}, true) == VelocityRoute::None);
}

TEST_CASE("Every consumer alone forces the velocity pass", "[render][velocity]")
{
	VelocityConsumers cases[10] = {};
	cases[0].DebugView = true;
	cases[1].Taa = true;
	cases[2].Dlaa = true;
	cases[3].NeuralTemporal = true;
	cases[4].DatasetExport = true;
	cases[5].GiTemporal = true;
	cases[6].ReflectionTemporal = true;
	cases[7].AoTemporal = true;
	cases[8].ShadowTemporal = true;
	cases[9].PrevColorGather = true;

	for (const VelocityConsumers& c : cases)
	{
		CHECK(AnyVelocityConsumer(c));
		CHECK(ChooseVelocityRoute(c, true) == VelocityRoute::SeparatePass);
	}
}

TEST_CASE("A missing velocity target disables the route", "[render][velocity]")
{
	VelocityConsumers c;
	c.Taa = true;
	CHECK(ChooseVelocityRoute(c, false) == VelocityRoute::None);
}
