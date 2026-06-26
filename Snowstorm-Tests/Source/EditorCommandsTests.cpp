#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/ComponentRegistration.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/World/EditorCommands.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <nlohmann/json.hpp>

using namespace Snowstorm;

namespace
{
	// Component reflection must be registered before SceneSerializer can round-trip an entity. The app
	// does this in Application's constructor; tests have no Application, so do it once here.
	struct RttrInit
	{
		RttrInit() { InitializeRTTR(); }
	};
	const RttrInit g_rttrInit;

	// Helper: the world's entity count (entities carry IDComponent).
	size_t EntityCount(World& world)
	{
		size_t n = 0;
		for (auto view = world.GetRegistry().view<IDComponent>(); const auto e : view)
		{
			(void)e;
			++n;
		}
		return n;
	}
}

TEST_CASE("World::FindEntityByUUID resolves entities by stable id", "[editor][world]")
{
	World world;
	const Entity a = world.CreateEntity("A");
	const Entity b = world.CreateEntity("B");

	const UUID idA = a.GetComponent<IDComponent>().Id;
	const UUID idB = b.GetComponent<IDComponent>().Id;

	REQUIRE(world.FindEntityByUUID(idA).Handle() == a.Handle());
	REQUIRE(world.FindEntityByUUID(idB).Handle() == b.Handle());
	REQUIRE_FALSE(world.FindEntityByUUID(UUID{123456789}).IsValid());
}

TEST_CASE("SceneSerializer round-trips a single entity preserving UUID + components", "[editor][serialize]")
{
	World world;
	Entity e = world.CreateEntity("Original");
	e.AddComponent<TransformComponent>().Position = glm::vec3(1.0f, 2.0f, 3.0f);
	const UUID id = e.GetComponent<IDComponent>().Id;

	nlohmann::json snap;
	REQUIRE(SceneSerializer::SerializeEntity(e, snap));

	// Destroy the original, then restore from the snapshot.
	world.DestroyEntity(e);
	world.FlushDestroyQueue();
	REQUIRE_FALSE(world.FindEntityByUUID(id).IsValid());

	const Entity restored = SceneSerializer::DeserializeEntity(world, snap);
	REQUIRE(restored.IsValid());
	REQUIRE(restored.GetComponent<IDComponent>().Id == id); // same identity
	REQUIRE(restored.GetComponent<TagComponent>().Tag == "Original");
	REQUIRE(restored.HasComponent<TransformComponent>());
	REQUIRE(restored.GetComponent<TransformComponent>().Position.y == 2.0f);
}

TEST_CASE("TransformCommand undo/redo restores before/after", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("Movable");
	e.AddComponent<TransformComponent>().Position = glm::vec3(0.0f);
	const UUID id = e.GetComponent<IDComponent>().Id;

	TransformComponent before = e.GetComponent<TransformComponent>();
	TransformComponent after = before;
	after.Position = glm::vec3(5.0f, 0.0f, 0.0f);
	e.PatchComponent<TransformComponent>([&](TransformComponent& t)
	                                     { t = after; });

	EditorHistorySingleton history;
	history.Push(CreateRef<TransformCommand>(id, before, after));

	REQUIRE(history.CanUndo());
	history.Undo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TransformComponent>().Position.x == 0.0f);

	REQUIRE(history.CanRedo());
	history.Redo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TransformComponent>().Position.x == 5.0f);
}

TEST_CASE("RenameCommand undo/redo restores before/after tag", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("OldName");
	const UUID id = e.GetComponent<IDComponent>().Id;

	e.WriteComponent<TagComponent>().Tag = "NewName";

	EditorHistorySingleton history;
	history.Push(CreateRef<RenameCommand>(id, "OldName", "NewName"));

	history.Undo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TagComponent>().Tag == "OldName");
	history.Redo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TagComponent>().Tag == "NewName");
}

TEST_CASE("AddEntityCommand undo removes, redo restores (create / duplicate path)", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("Created");
	e.AddComponent<TransformComponent>().Position = glm::vec3(7.0f);
	const UUID id = e.GetComponent<IDComponent>().Id;

	EditorHistorySingleton history;
	history.Push(CreateRef<AddEntityCommand>(id, "Create Entity"));

	// Undo removes the entity (deferred destroy → flush).
	history.Undo(world);
	world.FlushDestroyQueue();
	REQUIRE_FALSE(world.FindEntityByUUID(id).IsValid());

	// Redo restores it with the same identity + components.
	history.Redo(world);
	const Entity restored = world.FindEntityByUUID(id);
	REQUIRE(restored.IsValid());
	REQUIRE(restored.GetComponent<TransformComponent>().Position.x == 7.0f);
}

TEST_CASE("DeleteEntityCommand undo restores snapshot, redo deletes again", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("Doomed");
	e.AddComponent<TransformComponent>().Position = glm::vec3(9.0f);
	const UUID id = e.GetComponent<IDComponent>().Id;

	// Mirror the hierarchy panel: snapshot, then destroy, then push the command.
	nlohmann::json snap;
	REQUIRE(SceneSerializer::SerializeEntity(e, snap));
	world.DestroyEntity(e);
	world.FlushDestroyQueue();

	EditorHistorySingleton history;
	history.Push(CreateRef<DeleteEntityCommand>(id, snap));

	// Undo brings it back.
	history.Undo(world);
	const Entity restored = world.FindEntityByUUID(id);
	REQUIRE(restored.IsValid());
	REQUIRE(restored.GetComponent<TransformComponent>().Position.x == 9.0f);

	// Redo deletes it again.
	history.Redo(world);
	world.FlushDestroyQueue();
	REQUIRE_FALSE(world.FindEntityByUUID(id).IsValid());
}

TEST_CASE("ComponentEditCommand undo/redo restores serialized before/after", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("Edited");
	e.AddComponent<TransformComponent>().Position = glm::vec3(1.0f, 1.0f, 1.0f);
	const UUID id = e.GetComponent<IDComponent>().Id;

	const std::string typeName = "Snowstorm::TransformComponent";

	// Snapshot before, mutate (as the inspector would), snapshot after.
	nlohmann::json before;
	REQUIRE(SceneSerializer::SerializeEntity(e, before));
	// Pull just the component sub-objects so the command stores per-component JSON.
	const nlohmann::json beforeComp = before["Components"][typeName];

	e.PatchComponent<TransformComponent>([](TransformComponent& t)
	                                     { t.Position = glm::vec3(5.0f, 6.0f, 7.0f); });

	nlohmann::json after;
	REQUIRE(SceneSerializer::SerializeEntity(e, after));
	const nlohmann::json afterComp = after["Components"][typeName];

	EditorHistorySingleton history;
	history.Push(CreateRef<ComponentEditCommand>(id, typeName, beforeComp, afterComp));

	history.Undo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TransformComponent>().Position.x == 1.0f);

	history.Redo(world);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TransformComponent>().Position.x == 5.0f);
	REQUIRE(world.FindEntityByUUID(id).GetComponent<TransformComponent>().Position.z == 7.0f);
}

TEST_CASE("History: a new action clears the redo stack", "[editor][undo]")
{
	World world;
	Entity e = world.CreateEntity("E");
	const UUID id = e.GetComponent<IDComponent>().Id;
	e.AddComponent<TransformComponent>();

	EditorHistorySingleton history;

	TransformComponent before = e.GetComponent<TransformComponent>();
	TransformComponent after = before;
	after.Position = glm::vec3(1.0f);
	e.PatchComponent<TransformComponent>([&](TransformComponent& t)
	                                     { t = after; });
	history.Push(CreateRef<TransformCommand>(id, before, after));

	history.Undo(world);
	REQUIRE(history.CanRedo());

	// A fresh action must drop the redo branch.
	history.Push(CreateRef<RenameCommand>(id, "E", "E2"));
	REQUIRE_FALSE(history.CanRedo());
}
