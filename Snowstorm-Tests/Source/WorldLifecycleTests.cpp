#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/World/EditorHooksSingleton.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	size_t EntityCount(World& world)
	{
		size_t n = 0;
		for ([[maybe_unused]] const auto e : world.GetRegistry().view<entt::entity>())
		{
			++n;
		}
		return n;
	}
}

TEST_CASE("CreateEntity assigns a unique UUID and a default name", "[world]")
{
	World world;
	Entity a = world.CreateEntity();
	Entity b = world.CreateEntity();

	REQUIRE(a.IsValid());
	REQUIRE(b.IsValid());
	CHECK(a.GetComponent<TagComponent>().Tag == "Entity");
	CHECK(b.GetComponent<TagComponent>().Tag == "Entity");
	CHECK_FALSE(a.GetComponent<IDComponent>().Id == b.GetComponent<IDComponent>().Id);
}

TEST_CASE("CreateEntity keeps a given name", "[world]")
{
	World world;
	Entity e = world.CreateEntity("Lamp");
	CHECK(e.GetComponent<TagComponent>().Tag == "Lamp");
}

TEST_CASE("CreateEntityWithUUID keeps the UUID and defaults an empty name", "[world]")
{
	World world;
	const UUID id{1234567};

	Entity named = world.CreateEntityWithUUID(id, "Crate");
	CHECK(named.GetComponent<IDComponent>().Id == id);
	CHECK(named.GetComponent<TagComponent>().Tag == "Crate");

	Entity unnamed = world.CreateEntityWithUUID(UUID{42});
	CHECK(unnamed.GetComponent<IDComponent>().Id == UUID{42});
	CHECK(unnamed.GetComponent<TagComponent>().Tag == "Entity");
}

TEST_CASE("FindEntityByUUID returns the matching entity or an invalid one", "[world]")
{
	World world;
	Entity a = world.CreateEntityWithUUID(UUID{7}, "A");
	world.CreateEntityWithUUID(UUID{8}, "B");

	CHECK(world.FindEntityByUUID(UUID{7}).Handle() == a.Handle());
	CHECK_FALSE(world.FindEntityByUUID(UUID{9}).IsValid());
}

TEST_CASE("DestroyEntity defers until FlushDestroyQueue", "[world]")
{
	World world;
	Entity e = world.CreateEntity("Doomed");
	Entity keep = world.CreateEntity("Keep");

	world.DestroyEntity(e);
	CHECK(e.IsValid());
	CHECK(EntityCount(world) == 2);

	world.FlushDestroyQueue();
	CHECK_FALSE(e.IsValid());
	CHECK(keep.IsValid());
	CHECK(EntityCount(world) == 1);
}

TEST_CASE("DestroyEntity ignores invalid entities and tolerates duplicates", "[world]")
{
	World world;
	Entity e = world.CreateEntity();

	world.DestroyEntity(Entity{});
	world.DestroyEntity(e);
	world.DestroyEntity(e);
	world.FlushDestroyQueue();
	CHECK_FALSE(e.IsValid());
	CHECK(EntityCount(world) == 0);

	world.FlushDestroyQueue(); // empty queue is a no-op
	CHECK(EntityCount(world) == 0);
}

TEST_CASE("FlushDestroyQueue empties the queue", "[world]")
{
	World world;
	Entity e = world.CreateEntity();
	world.DestroyEntity(e);
	world.FlushDestroyQueue();

	// A new entity may recycle the handle; a second flush must not destroy it.
	Entity fresh = world.CreateEntity();
	world.FlushDestroyQueue();
	CHECK(fresh.IsValid());
}

TEST_CASE("Clear removes every entity", "[world]")
{
	World world;
	Entity a = world.CreateEntity();
	Entity keepTagged = world.CreateEntity();
	keepTagged.AddComponent<DoNotSerializeComponent>();

	world.Clear();
	CHECK_FALSE(a.IsValid());
	CHECK_FALSE(keepTagged.IsValid());
	CHECK(EntityCount(world) == 0);
}

TEST_CASE("ClearSceneEntities keeps DoNotSerialize entities and bumps the generation", "[world]")
{
	World world;
	Entity scene = world.CreateEntity("Scene");
	Entity editorCam = world.CreateEntity("Editor Camera");
	editorCam.AddComponent<DoNotSerializeComponent>();

	const uint64_t before = world.SceneGeneration();
	world.ClearSceneEntities();

	CHECK_FALSE(scene.IsValid());
	CHECK(editorCam.IsValid());
	CHECK(EntityCount(world) == 1);
	CHECK(world.SceneGeneration() == before + 1);

	world.ClearSceneEntities();
	CHECK(world.SceneGeneration() == before + 2);
}

TEST_CASE("Clear leaves the scene generation alone", "[world]")
{
	World world;
	const uint64_t before = world.SceneGeneration();
	world.Clear();
	CHECK(world.SceneGeneration() == before);
}

TEST_CASE("ClearSceneEntities calls the OnSceneCleared hook when set", "[world]")
{
	World world;
	int calls = 0;
	world.GetSingleton<EditorHooksSingleton>().OnSceneCleared = [&calls]
	{ ++calls; };

	world.ClearSceneEntities();
	CHECK(calls == 1);

	world.Clear();
	CHECK(calls == 1);
}
