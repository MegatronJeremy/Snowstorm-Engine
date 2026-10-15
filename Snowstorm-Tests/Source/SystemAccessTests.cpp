#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/ECS/SystemManager.hpp"
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
