#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;

namespace
{
	namespace fs = std::filesystem;

	size_t CountEntities(const World& world)
	{
		size_t n = 0;
		for (auto view = world.GetRegistry().view<IDComponent>(); [[maybe_unused]] const auto e : view)
		{
			++n;
		}
		return n;
	}

	struct TempDir
	{
		fs::path Path = fs::temp_directory_path() / "ss_scene_roundtrip_test";

		TempDir()
		{
			fs::remove_all(Path);
			fs::create_directories(Path);
		}

		~TempDir()
		{
			std::error_code ec;
			fs::remove_all(Path, ec);
		}
	};

	void BuildScene(World& world)
	{
		Entity a = world.CreateEntityWithUUID(UUID{1001}, "Floor");
		auto& ta = a.AddComponent<TransformComponent>();
		ta.Position = {1.0f, 2.0f, 3.0f};
		ta.Rotation = {0.25f, 0.5f, 0.75f};
		ta.Scale = {4.0f, 5.0f, 6.0f};
		a.AddComponent<MeshComponent>().MeshHandle = AssetHandle{111};
		a.AddComponent<MaterialComponent>().Material = AssetHandle{222};

		Entity b = world.CreateEntityWithUUID(UUID{1002}, "Crate");
		b.AddComponent<TransformComponent>().Position = {-7.5f, 0.0f, 9.0f};

		world.CreateEntityWithUUID(UUID{1003}, "Empty");
	}

	void CheckScene(const World& world)
	{
		REQUIRE(CountEntities(world) == 3);

		const Entity a = world.FindEntityByUUID(UUID{1001});
		REQUIRE(a);
		CHECK(a.GetComponent<TagComponent>().Tag == "Floor");
		const auto& ta = a.GetComponent<TransformComponent>();
		CHECK(ta.Position == glm::vec3{1.0f, 2.0f, 3.0f});
		CHECK(ta.Rotation == glm::vec3{0.25f, 0.5f, 0.75f});
		CHECK(ta.Scale == glm::vec3{4.0f, 5.0f, 6.0f});
		CHECK(a.GetComponent<MeshComponent>().MeshHandle == AssetHandle{111});
		CHECK(a.GetComponent<MaterialComponent>().Material == AssetHandle{222});

		const Entity b = world.FindEntityByUUID(UUID{1002});
		REQUIRE(b);
		CHECK(b.GetComponent<TagComponent>().Tag == "Crate");
		CHECK(b.GetComponent<TransformComponent>().Position == glm::vec3{-7.5f, 0.0f, 9.0f});
		CHECK_FALSE(b.HasComponent<MeshComponent>());

		const Entity c = world.FindEntityByUUID(UUID{1003});
		REQUIRE(c);
		CHECK(c.GetComponent<TagComponent>().Tag == "Empty");
		CHECK_FALSE(c.HasComponent<TransformComponent>());
	}
}

TEST_CASE("A whole scene round-trips through a file", "[serialize]")
{
	TempDir dir;
	const std::string file = (dir.Path / "scene.world").string();

	World world;
	BuildScene(world);
	REQUIRE(SceneSerializer::Serialize(world, file));
	REQUIRE(fs::exists(file));

	World loaded;
	REQUIRE(SceneSerializer::Deserialize(loaded, file));
	CheckScene(loaded);
}

TEST_CASE("A whole scene round-trips through a string", "[serialize]")
{
	World world;
	BuildScene(world);
	const std::string text = SceneSerializer::SerializeToString(world);
	REQUIRE_FALSE(text.empty());

	World loaded;
	REQUIRE(SceneSerializer::DeserializeFromString(loaded, text));
	CheckScene(loaded);
}

TEST_CASE("DoNotSerialize entities are left out of the scene", "[serialize]")
{
	World world;
	BuildScene(world);
	Entity editorOwned = world.CreateEntityWithUUID(UUID{2001}, "EditorCamera");
	editorOwned.AddComponent<DoNotSerializeComponent>();

	World loaded;
	REQUIRE(SceneSerializer::DeserializeFromString(loaded, SceneSerializer::SerializeToString(world)));
	CHECK(CountEntities(loaded) == 3);
	CHECK_FALSE(loaded.FindEntityByUUID(UUID{2001}));
}

TEST_CASE("Loading adds to the world instead of replacing it", "[serialize]")
{
	World world;
	BuildScene(world);
	const std::string text = SceneSerializer::SerializeToString(world);

	World loaded;
	loaded.CreateEntityWithUUID(UUID{3001}, "Existing");
	REQUIRE(SceneSerializer::DeserializeFromString(loaded, text));
	CHECK(CountEntities(loaded) == 4);
	CHECK(loaded.FindEntityByUUID(UUID{3001}));
}

TEST_CASE("Missing and malformed scene files fail without adding entities", "[serialize]")
{
	TempDir dir;
	World world;

	CHECK_FALSE(SceneSerializer::Deserialize(world, (dir.Path / "absent.world").string()));

	const auto writeFile = [&](const char* name, const std::string& body)
	{
		const fs::path p = dir.Path / name;
		std::ofstream(p) << body;
		return p.string();
	};

	CHECK_FALSE(SceneSerializer::Deserialize(world, writeFile("garbage.world", "{ not json")));
	CHECK_FALSE(SceneSerializer::Deserialize(world, writeFile("empty.world", "")));
	CHECK_FALSE(SceneSerializer::Deserialize(world, writeFile("noentities.world", R"({"Scene":{}})")));
	CHECK_FALSE(SceneSerializer::Deserialize(world, writeFile("badentities.world", R"({"Entities":{}})")));
	CHECK_FALSE(SceneSerializer::DeserializeFromString(world, "[1,2"));

	CHECK(CountEntities(world) == 0);
}

TEST_CASE("Serialize reports failure for an unwritable path", "[serialize]")
{
	TempDir dir;
	World world;
	BuildScene(world);
	CHECK_FALSE(SceneSerializer::Serialize(world, (dir.Path / "no_such_dir" / "scene.world").string()));
}
