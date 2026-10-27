#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/SpriteComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <nlohmann/json.hpp>

using namespace Snowstorm;

namespace
{
	nlohmann::json EntityJson(const std::string& components)
	{
		return nlohmann::json::parse(R"({"UUID":"42","Name":"E","Components":)" + components + "}");
	}

	void Reload(const World& world, World& loaded)
	{
		REQUIRE(SceneSerializer::DeserializeFromString(loaded, SceneSerializer::SerializeToString(world)));
	}
}

TEST_CASE("SpriteComponent round-trips every reflected property", "[serialize][sprite]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Sprite");
	auto& s = e.AddComponent<SpriteComponent>();
	s.Texture = AssetHandle{123456789012345ull};
	s.Size = {2.5f, 0.75f};
	s.Pivot = {0.0f, 1.0f};
	s.Tint = {0.25f, 0.5f, 0.75f, 0.125f};
	s.FlipX = true;
	s.FlipY = true;
	s.Layer = -3;
	s.Order = 17;
	s.Filter = SpriteFilter::Linear;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(r);
	REQUIRE(r.HasComponent<SpriteComponent>());
	const auto& g = r.GetComponent<SpriteComponent>();
	CHECK(g.Texture.Value() == 123456789012345ull);
	CHECK(g.Size == glm::vec2{2.5f, 0.75f});
	CHECK(g.Pivot == glm::vec2{0.0f, 1.0f});
	CHECK(g.Tint == glm::vec4{0.25f, 0.5f, 0.75f, 0.125f});
	CHECK(g.FlipX);
	CHECK(g.FlipY);
	CHECK(g.Layer == -3);
	CHECK(g.Order == 17);
	CHECK(g.Filter == SpriteFilter::Linear);
}

TEST_CASE("SpriteComponent writes no runtime state", "[serialize][sprite]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Sprite");
	auto& s = e.AddComponent<SpriteComponent>();
	s.ResolvedFrom = AssetHandle{777ull};

	const auto root = nlohmann::json::parse(SceneSerializer::SerializeToString(world));
	const auto& block = root["Entities"][0]["Components"]["Snowstorm::SpriteComponent"];
	REQUIRE(block.is_object());
	CHECK_FALSE(block.contains("View"));
	CHECK_FALSE(block.contains("ResolvedFrom"));

	World loaded;
	Reload(world, loaded);
	const auto& g = loaded.FindEntityByUUID(UUID{1}).GetComponent<SpriteComponent>();
	CHECK(g.View == nullptr);
	CHECK(g.ResolvedFrom.Value() == 0ull);
}

TEST_CASE("Missing sprite keys load the component defaults", "[serialize][sprite]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(world, EntityJson(R"({"Snowstorm::SpriteComponent": {}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<SpriteComponent>());

	const SpriteComponent d;
	const auto& g = e.GetComponent<SpriteComponent>();
	CHECK(g.Texture.Value() == d.Texture.Value());
	CHECK(g.Size == d.Size);
	CHECK(g.Pivot == d.Pivot);
	CHECK(g.Tint == d.Tint);
	CHECK(g.FlipX == d.FlipX);
	CHECK(g.FlipY == d.FlipY);
	CHECK(g.Layer == d.Layer);
	CHECK(g.Order == d.Order);
	CHECK(g.Filter == d.Filter);
}
