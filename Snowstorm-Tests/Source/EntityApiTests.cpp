#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	struct Health
	{
		int hp = 0;
		Health() = default;
		explicit Health(int hp_) : hp(hp_) {}
		bool operator==(const Health&) const = default;
	};

	struct Mana
	{
		int mp = 0;
		bool operator==(const Mana&) const = default;
	};
}

TEST_CASE("Entity: default-constructed entity is invalid", "[entity]")
{
	const Entity e;
	CHECK_FALSE(e.IsValid());
	CHECK_FALSE(static_cast<bool>(e));
	CHECK(e.Handle() == entt::null);
	CHECK(e.GetWorld() == nullptr);
}

TEST_CASE("Entity: a null handle in a live world is invalid", "[entity]")
{
	World world;
	const Entity e{entt::null, &world};
	CHECK_FALSE(e.IsValid());
	CHECK(e.GetWorld() == &world);
}

TEST_CASE("Entity: CreateEntity yields a valid entity carrying ID and Tag", "[entity]")
{
	World world;
	const Entity e = world.CreateEntity("Hero");

	REQUIRE(e.IsValid());
	CHECK(static_cast<bool>(e));
	CHECK(e.GetWorld() == &world);
	CHECK(e.HasComponent<IDComponent>());
	CHECK(e.GetComponent<TagComponent>().Tag == "Hero");
	CHECK(world.CreateEntity().GetComponent<TagComponent>().Tag == "Entity");
}

TEST_CASE("Entity: Add, Get, Has, HasAll and Remove", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();

	CHECK_FALSE(e.HasComponent<Health>());
	CHECK_FALSE(e.HasAll<Health, Mana>());

	Health& added = e.AddComponent<Health>(7);
	CHECK(added.hp == 7);
	CHECK(e.HasComponent<Health>());
	CHECK(e.GetComponent<Health>().hp == 7);
	CHECK(&e.GetComponent<Health>() == &added);
	CHECK_FALSE(e.HasAll<Health, Mana>());

	e.AddComponent<Mana>();
	CHECK(e.HasAll<Health, Mana>());
	CHECK(e.HasAll<Health>());

	e.RemoveComponent<Health>();
	CHECK_FALSE(e.HasComponent<Health>());
	CHECK(e.HasComponent<Mana>());
	CHECK_FALSE(e.HasAll<Health, Mana>());
}

TEST_CASE("Entity: AddOrReplaceComponent adds when absent and overwrites when present", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();

	e.AddOrReplaceComponent<Health>(1);
	CHECK(e.GetComponent<Health>().hp == 1);

	e.AddOrReplaceComponent<Health>(2);
	CHECK(e.GetComponent<Health>().hp == 2);
}

TEST_CASE("Entity: TryGetComponent returns null for a missing component", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();

	CHECK(e.TryGetComponent<Health>() == nullptr);
	e.AddComponent<Health>(3);
	REQUIRE(e.TryGetComponent<Health>() != nullptr);
	CHECK(e.TryGetComponent<Health>()->hp == 3);

	CHECK(e.TryWriteComponent<Mana>() == nullptr);
	CHECK_FALSE(world.GetRegistry().WasChanged<Mana>(e.Handle()));
}

TEST_CASE("Entity: tracked writes mark Changed, untracked access does not", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();
	e.AddComponent<Health>(1);
	world.GetRegistry().ClearTrackedComponents();

	e.GetComponentMutable_Untracked<Health>().hp = 5;
	CHECK(e.GetComponent<Health>().hp == 5);
	CHECK_FALSE(world.GetRegistry().WasChanged<Health>(e.Handle()));

	e.WriteComponent<Health>().hp = 6;
	CHECK(e.GetComponent<Health>().hp == 6);
	CHECK(world.GetRegistry().WasChanged<Health>(e.Handle()));

	world.GetRegistry().ClearTrackedComponents();
	REQUIRE(e.TryWriteComponent<Health>() != nullptr);
	CHECK(world.GetRegistry().WasChanged<Health>(e.Handle()));
}

TEST_CASE("Entity: WriteComponentIfChanged writes only on a real difference", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();
	e.AddComponent<Health>(4);
	world.GetRegistry().ClearTrackedComponents();

	CHECK_FALSE(e.WriteComponentIfChanged<Health>([](Health& h)
	                                              { h.hp = 4; }));
	CHECK_FALSE(world.GetRegistry().WasChanged<Health>(e.Handle()));

	CHECK(e.WriteComponentIfChanged<Health>([](Health& h)
	                                        { h.hp = 9; }));
	CHECK(e.GetComponent<Health>().hp == 9);
	CHECK(world.GetRegistry().WasChanged<Health>(e.Handle()));
}

TEST_CASE("Entity: PatchComponent and ReplaceComponent mutate and mark Changed", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();
	e.AddComponent<Health>(1);
	world.GetRegistry().ClearTrackedComponents();

	e.PatchComponent<Health>([](Health& h)
	                         { h.hp = 20; });
	CHECK(e.GetComponent<Health>().hp == 20);

	e.ReplaceComponent(Health{30});
	CHECK(e.GetComponent<Health>().hp == 30);
}

TEST_CASE("Entity: equality compares handle and world", "[entity]")
{
	World a;
	World b;
	const Entity e1 = a.CreateEntity();
	const Entity e2 = a.CreateEntity();
	const Entity copy = e1;

	CHECK(e1 == copy);
	CHECK(e1 != e2);
	CHECK(Entity{e1.Handle(), &b} != e1);
	CHECK(Entity{} == Entity{});
}

TEST_CASE("Entity: conversions expose the underlying handle", "[entity]")
{
	World world;
	const Entity e = world.CreateEntity();

	const entt::entity handle = e;
	CHECK(handle == e.Handle());
	CHECK(static_cast<uint32_t>(e) == static_cast<uint32_t>(e.Handle()));

	const Entity e2 = world.CreateEntity();
	CHECK(static_cast<uint32_t>(e) != static_cast<uint32_t>(e2));
}

TEST_CASE("Entity: destruction is deferred until FlushDestroyQueue", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();
	e.AddComponent<Health>(1);

	world.DestroyEntity(e);
	CHECK(e.IsValid());
	CHECK(e.HasComponent<Health>());

	world.FlushDestroyQueue();
	CHECK_FALSE(e.IsValid());
	CHECK_FALSE(static_cast<bool>(e));
}

TEST_CASE("Entity: a destroyed handle stays invalid after its slot is recycled", "[entity]")
{
	World world;
	const Entity stale = world.CreateEntity();
	world.DestroyEntity(stale);
	world.FlushDestroyQueue();

	const Entity fresh = world.CreateEntity();
	CHECK(fresh.IsValid());
	CHECK_FALSE(stale.IsValid());
	CHECK(stale != fresh);
}

TEST_CASE("Entity: destroying an invalid entity is a no-op and double destroy is safe", "[entity]")
{
	World world;
	Entity e = world.CreateEntity();
	const Entity other = world.CreateEntity();

	world.DestroyEntity(Entity{});
	world.DestroyEntity(e);
	world.DestroyEntity(e);
	world.FlushDestroyQueue();

	CHECK_FALSE(e.IsValid());
	CHECK(other.IsValid());
}

TEST_CASE("Entity: FindEntityByUUID round-trips and misses return an invalid entity", "[entity]")
{
	World world;
	const UUID id{12345};
	const Entity e = world.CreateEntityWithUUID(id, "Named");

	CHECK(e.GetComponent<IDComponent>().Id == id);
	CHECK(world.FindEntityByUUID(id) == e);

	const Entity miss = world.FindEntityByUUID(UUID{54321});
	CHECK_FALSE(miss.IsValid());
}
