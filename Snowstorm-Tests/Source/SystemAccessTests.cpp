#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemSchedule.hpp"
#include "Snowstorm/Systems/CoreSystems.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	struct A
	{
	};
	struct B
	{
	};
	struct Res
	{
	};

	SystemAccess Declared()
	{
		SystemAccess a;
		a.ReadComponents<Res>(); // marks it declared
		return a;
	}
}

TEST_CASE("SystemAccess: undeclared is exclusive and conflicts with everything", "[ecs]")
{
	const SystemAccess undeclared;
	SystemAccess reader = Declared();
	reader.ReadComponents<A>();
	CHECK(undeclared.IsStructural());
	CHECK(undeclared.IsMainThread);
	CHECK_FALSE(undeclared.Declared);
	CHECK(undeclared.ConflictsWith(reader));
	CHECK(reader.ConflictsWith(undeclared));
}

TEST_CASE("SystemAccess: conflicts need a write on a shared type", "[ecs]")
{
	SystemAccess r1 = Declared(), r2 = Declared(), w = Declared(), other = Declared();
	r1.ReadComponents<A>();
	r2.ReadComponents<A>();
	w.WriteComponents<A>();
	other.WriteComponents<B>().ReadResources<Res>();

	CHECK_FALSE(r1.ConflictsWith(r2));
	CHECK(r1.ConflictsWith(w));
	CHECK(w.ConflictsWith(r1));
	CHECK_FALSE(w.ConflictsWith(other));

	SystemAccess resWriter = Declared();
	resWriter.WriteResources<Res>();
	CHECK(resWriter.ConflictsWith(other));
	CHECK(other.ConflictsWith(resWriter));
}

TEST_CASE("SystemAccess: structural systems conflict even when disjoint", "[ecs]")
{
	SystemAccess s;
	s.Structural().WriteComponents<A>();
	SystemAccess o = Declared();
	o.ReadComponents<B>();
	CHECK(s.Declared);
	CHECK(s.ConflictsWith(o));
}

TEST_CASE("Every core system declares its access", "[ecs]")
{
	World world;
	RegisterCoreSystems(world);
	const auto all = world.GetSystemManager().DeclaredAccess();
	REQUIRE(all.size() >= 19);
	for (const auto& [name, access] : all)
	{
		INFO(name);
		CHECK(access.Declared);
	}
}

TEST_CASE("BuildSchedule: disjoint systems share a stage, conflicts keep registration order", "[ecs]")
{
	SystemAccess w = Declared(), r = Declared(), other = Declared(), r2 = Declared();
	w.WriteComponents<A>();
	r.ReadComponents<A>();
	other.WriteComponents<B>();
	r2.ReadComponents<A>();

	using Stages = SystemSchedule;
	CHECK(BuildSchedule({}).empty());
	CHECK(BuildSchedule({w, other}) == Stages{{0, 1}});
	CHECK(BuildSchedule({w, r}) == Stages{{0}, {1}});
	CHECK(BuildSchedule({r, r2}) == Stages{{0, 1}});
	// w -> r and w -> r2, the two readers share stage 1; `other` is free of both.
	CHECK(BuildSchedule({w, r, other, r2}) == Stages{{0, 2}, {1, 3}});
}

TEST_CASE("BuildSchedule: structural and undeclared systems run alone", "[ecs]")
{
	SystemAccess a = Declared(), b = Declared(), structural, undeclared;
	a.ReadComponents<A>();
	b.ReadComponents<B>();
	structural.Structural().WriteComponents<A>();

	using Stages = SystemSchedule;
	CHECK(BuildSchedule({a, structural, b}) == Stages{{0}, {1}, {2}});
	CHECK(BuildSchedule({a, undeclared, b}) == Stages{{0}, {1}, {2}});
}

TEST_CASE("BuildSchedule: a tracked read follows a tracked write through the pseudo-resource", "[ecs]")
{
	SystemAccess mutator = Declared(), reader = Declared();
	mutator.WriteComponents<A>().WriteResources<TrackingMapResource>();
	reader.ReadComponents<B>().ReadResources<TrackingMapResource>();
	// Disjoint components, but the unsynchronised tracking maps force an order.
	CHECK(BuildSchedule({mutator, reader}) == SystemSchedule{{0}, {1}});
}

TEST_CASE("BuildSchedule: core systems schedule every system exactly once, in order per stage", "[ecs]")
{
	World world;
	RegisterCoreSystems(world);
	std::vector<SystemAccess> access;
	for (const auto& [name, a] : world.GetSystemManager().DeclaredAccess())
	{
		access.push_back(a);
	}
	const auto stages = BuildSchedule(access);
	size_t total = 0;
	for (const auto& stage : stages)
	{
		REQUIRE_FALSE(stage.empty());
		CHECK(std::is_sorted(stage.begin(), stage.end()));
		total += stage.size();
	}
	CHECK(total == access.size());
}
