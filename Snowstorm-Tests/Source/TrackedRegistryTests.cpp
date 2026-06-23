#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/ECS/TrackedRegistry.hpp"

using namespace Snowstorm;

namespace
{
	struct Position
	{
		int x = 0;
		int y = 0;
		Position() = default;
		Position(int x_, int y_) : x(x_), y(y_) {}
		bool operator==(const Position&) const = default;
	};

	struct Velocity
	{
		float v = 0.0f;
		bool operator==(const Velocity&) const = default;
	};
}

TEST_CASE("emplace marks a component added and changed for this frame", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e, 1, 2);

	REQUIRE(reg.any_of<Position>(e));
	REQUIRE(reg.WasAdded<Position>(e));
	REQUIRE(reg.WasChanged<Position>(e));
	REQUIRE(reg.AddedView<Position>().contains(e));
	REQUIRE(reg.Read<Position>(e) == Position{1, 2});
}

TEST_CASE("ClearTrackedComponents resets per-frame tracking but keeps components", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e);
	reg.ClearTrackedComponents();

	REQUIRE_FALSE(reg.WasAdded<Position>(e));
	REQUIRE(reg.AddedView<Position>().empty());
	REQUIRE(reg.any_of<Position>(e)); // component itself survives
}

TEST_CASE("remove marks a component removed for this frame", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e);
	reg.ClearTrackedComponents();

	reg.remove<Position>(e);

	REQUIRE_FALSE(reg.any_of<Position>(e));
	REQUIRE(reg.WasRemoved<Position>(e));
	REQUIRE(reg.RemovedView<Position>().contains(e));
}

TEST_CASE("WriteIfChanged only marks Changed when the value actually changes", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e, 1, 1);
	reg.ClearTrackedComponents();

	const bool noop = reg.WriteIfChanged<Position>(e, [](Position& p) { p.x = 1; p.y = 1; });
	REQUIRE_FALSE(noop);
	REQUIRE_FALSE(reg.WasChanged<Position>(e));

	const bool changed = reg.WriteIfChanged<Position>(e, [](Position& p) { p.x = 5; });
	REQUIRE(changed);
	REQUIRE(reg.WasChanged<Position>(e));
	REQUIRE(reg.ChangedView<Position>().contains(e));
	REQUIRE(reg.Read<Position>(e).x == 5);
}

TEST_CASE("AddedView requires ALL listed component types (AND semantics)", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e);

	REQUIRE(reg.AddedView<Position>().contains(e));
	REQUIRE_FALSE((reg.AddedView<Position, Velocity>().contains(e)));

	reg.emplace<Velocity>(e);
	REQUIRE((reg.AddedView<Position, Velocity>().contains(e)));
}

TEST_CASE("Clear wipes entities and all tracking state", "[ecs]")
{
	TrackedRegistry reg;
	const auto e = reg.create();
	reg.emplace<Position>(e);

	reg.Clear();

	REQUIRE_FALSE(reg.valid(e));
	REQUIRE(reg.AddedView<Position>().empty());
}
