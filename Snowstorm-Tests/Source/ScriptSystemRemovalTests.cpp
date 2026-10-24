#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	int g_Destroyed = 0;
	int g_Deleted = 0;

	class CountingScript final : public ScriptableEntity
	{
	public:
		~CountingScript() override { ++g_Deleted; }

	protected:
		void OnDestroy() override { ++g_Destroyed; }
	};

	void RunFrame(World& world, ScriptSystem& system)
	{
		system.Execute(Timestep{});
		world.GetRegistry().ClearTrackedComponents();
	}
}

TEST_CASE("ScriptSystem runs OnDestroy and frees the instance of a removed NativeScriptComponent", "[scripts]")
{
	g_Destroyed = 0;
	g_Deleted = 0;
	World world;
	ScriptSystem system(&world);
	auto& reg = world.GetRegistry();
	const entt::entity e = reg.create();
	reg.emplace<NativeScriptComponent>(e).Bind<CountingScript>();

	RunFrame(world, system);
	CHECK(g_Destroyed == 0);

	reg.remove<NativeScriptComponent>(e);
	RunFrame(world, system);
	CHECK(g_Destroyed == 1);
	CHECK(g_Deleted == 1);

	RunFrame(world, system);
	CHECK(g_Destroyed == 1);
	CHECK(g_Deleted == 1);
}

TEST_CASE("ScriptSystem ignores a component added and removed before it ever ran", "[scripts]")
{
	g_Destroyed = 0;
	g_Deleted = 0;
	World world;
	ScriptSystem system(&world);
	auto& reg = world.GetRegistry();
	const entt::entity e = reg.create();
	reg.emplace<NativeScriptComponent>(e).Bind<CountingScript>();
	reg.remove<NativeScriptComponent>(e);

	RunFrame(world, system);
	CHECK(g_Destroyed == 0);
	CHECK(g_Deleted == 0);
}
