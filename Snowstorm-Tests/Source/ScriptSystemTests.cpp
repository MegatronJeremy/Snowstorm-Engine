#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/World/World.hpp"

#include <vector>

using namespace Snowstorm;

namespace
{
	std::vector<int>& CallLog()
	{
		static std::vector<int> log;
		return log;
	}

	constexpr int kCreate = 1;
	constexpr int kUpdate = 2;

	class RecordingScript final : public ScriptableEntity
	{
	protected:
		void OnCreate() override { CallLog().push_back(kCreate); }
		void OnUpdate(Timestep) override { CallLog().push_back(kUpdate); }
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
		reg.emplace<NativeScriptComponent>(e).Bind<RecordingScript>();
		return e;
	}

	void Release(World& world, const entt::entity e)
	{
		world.GetRegistry().Read<NativeScriptComponent>(e).DestroyScript();
	}
}

TEST_CASE("ScriptSystem does not tick in edit mode", "[scripts]")
{
	World world;
	ScriptSystem system(&world);
	CHECK_FALSE(system.RunsInEditMode());
}

TEST_CASE("ScriptSystem creates a script on its first frame and updates it in the same frame", "[scripts]")
{
	CallLog().clear();
	World world;
	ScriptSystem system(&world);
	const entt::entity e = AddScript(world);

	RunFrame(world, system);
	CHECK(CallLog() == std::vector<int>{kCreate, kUpdate});

	Release(world, e);
}

TEST_CASE("ScriptSystem only updates an already created script on later frames", "[scripts]")
{
	CallLog().clear();
	World world;
	ScriptSystem system(&world);
	const entt::entity e = AddScript(world);

	RunFrame(world, system);
	CallLog().clear();
	RunFrame(world, system);
	RunFrame(world, system);
	CHECK(CallLog() == std::vector<int>{kUpdate, kUpdate});

	Release(world, e);
}

TEST_CASE("ScriptSystem gives every script one create and one update per frame", "[scripts]")
{
	CallLog().clear();
	World world;
	ScriptSystem system(&world);
	const entt::entity a = AddScript(world);
	const entt::entity b = AddScript(world);

	RunFrame(world, system);
	int creates = 0;
	int updates = 0;
	for (const int call : CallLog())
	{
		creates += call == kCreate ? 1 : 0;
		updates += call == kUpdate ? 1 : 0;
	}
	CHECK(creates == 2);
	CHECK(updates == 2);
	CHECK(world.GetRegistry().Read<NativeScriptComponent>(a).Instance != nullptr);
	CHECK(world.GetRegistry().Read<NativeScriptComponent>(b).Instance != nullptr);

	Release(world, a);
	Release(world, b);
}

TEST_CASE("ScriptSystem on a world without scripts calls nothing", "[scripts]")
{
	CallLog().clear();
	World world;
	ScriptSystem system(&world);
	RunFrame(world, system);
	CHECK(CallLog().empty());
}
