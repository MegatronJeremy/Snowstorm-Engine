#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	struct Counts
	{
		int Created = 0;
		int Updated = 0;
		int Destroyed = 0;
		int Deleted = 0;
	};

	Counts g_Counts;

	class CountingScript final : public ScriptableEntity
	{
	public:
		~CountingScript() override { ++g_Counts.Deleted; }

	protected:
		void OnCreate() override { ++g_Counts.Created; }
		void OnUpdate(Timestep) override { ++g_Counts.Updated; }
		void OnDestroy() override { ++g_Counts.Destroyed; }
	};

	void RunFrame(World& world, ScriptSystem& system)
	{
		system.Execute(Timestep{});
		world.GetRegistry().ClearTrackedComponents();
	}

	entt::entity AddScript(World& world)
	{
		auto& reg = world.GetRegistry();
		const entt::entity e = reg.create();
		reg.emplace<NativeScriptComponent>(e).Bind<CountingScript>();
		return e;
	}
}

TEST_CASE("ScriptSystem runs OnDestroy on a script whose component was removed", "[script]")
{
	g_Counts = {};
	World world;
	ScriptSystem system(&world);
	const entt::entity e = AddScript(world);

	RunFrame(world, system);
	CHECK(g_Counts.Created == 1);
	CHECK(g_Counts.Updated == 1);
	CHECK(g_Counts.Destroyed == 0);

	world.GetRegistry().remove<NativeScriptComponent>(e);
	RunFrame(world, system);
	CHECK(g_Counts.Destroyed == 1);
	CHECK(g_Counts.Deleted == 1);
	CHECK(g_Counts.Updated == 1);

	RunFrame(world, system);
	CHECK(g_Counts.Destroyed == 1);
}

TEST_CASE("ScriptSystem ignores a removal for a script it never instantiated", "[script]")
{
	g_Counts = {};
	World world;
	ScriptSystem system(&world);
	const entt::entity e = AddScript(world);

	world.GetRegistry().remove<NativeScriptComponent>(e);
	RunFrame(world, system);
	CHECK(g_Counts.Created == 0);
	CHECK(g_Counts.Destroyed == 0);
}

TEST_CASE("ScriptSystem deletes live instances at teardown without OnDestroy", "[script]")
{
	g_Counts = {};
	World world;
	{
		ScriptSystem system(&world);
		AddScript(world);
		RunFrame(world, system);
		CHECK(g_Counts.Deleted == 0);
	}
	CHECK(g_Counts.Deleted == 1);
	CHECK(g_Counts.Destroyed == 0);
}

TEST_CASE("ScriptSystem gives an entity re-bound after removal a fresh instance", "[script]")
{
	g_Counts = {};
	World world;
	ScriptSystem system(&world);
	auto& reg = world.GetRegistry();
	const entt::entity e = AddScript(world);
	RunFrame(world, system);

	reg.remove<NativeScriptComponent>(e);
	RunFrame(world, system);
	reg.emplace<NativeScriptComponent>(e).Bind<CountingScript>();
	RunFrame(world, system);
	CHECK(g_Counts.Created == 2);
	CHECK(g_Counts.Destroyed == 1);
	CHECK(g_Counts.Deleted == 1);

	reg.remove<NativeScriptComponent>(e);
	RunFrame(world, system);
	CHECK(g_Counts.Destroyed == 2);
	CHECK(g_Counts.Deleted == 2);
}
