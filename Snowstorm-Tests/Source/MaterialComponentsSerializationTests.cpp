#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
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

	MaterialOverride Make(std::string name, const MaterialOverrideType type)
	{
		MaterialOverride o;
		o.Name = std::move(name);
		o.Type = type;
		return o;
	}
}

TEST_CASE("MaterialOverridesComponent round-trips every override type in order", "[serialize][material]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Overridden");
	auto& mo = e.AddComponent<MaterialOverridesComponent>();

	MaterialOverride rough = Make("Roughness", MaterialOverrideType::Float);
	rough.Scalar = 0.375f;
	MaterialOverride base = Make("BaseColor", MaterialOverrideType::Color);
	base.Color = {0.25f, 0.5f, 0.75f, 0.125f};
	MaterialOverride albedo = Make("AlbedoTexture", MaterialOverrideType::Texture);
	albedo.Texture = AssetHandle{123456789012345ull};
	mo.Overrides = {rough, base, albedo};

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(r);
	REQUIRE(r.HasComponent<MaterialOverridesComponent>());
	const auto& g = r.GetComponent<MaterialOverridesComponent>();
	REQUIRE(g.Overrides.size() == 3);
	CHECK(g.Overrides[0] == rough);
	CHECK(g.Overrides[1] == base);
	CHECK(g.Overrides[2] == albedo);
}

TEST_CASE("MaterialOverridesComponent with an empty list stays present and empty", "[serialize][material]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{2}, "Empty");
	e.AddComponent<MaterialOverridesComponent>();

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{2});
	REQUIRE(r);
	REQUIRE(r.HasComponent<MaterialOverridesComponent>());
	CHECK(r.GetComponent<MaterialOverridesComponent>().Overrides.empty());
}

TEST_CASE("MaterialOverridesComponent writes a sparse array keyed by name, type and value", "[serialize][material]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{3}, "Shape");
	MaterialOverride metal = Make("Metallic", MaterialOverrideType::Float);
	metal.Scalar = 1.0f;
	e.AddComponent<MaterialOverridesComponent>().Overrides = {metal};

	nlohmann::json out;
	REQUIRE(SceneSerializer::SerializeEntity(e, out));
	const auto& arr = out["Components"]["Snowstorm::MaterialOverridesComponent"]["Overrides"];
	REQUIRE(arr.is_array());
	REQUIRE(arr.size() == 1);
	CHECK(arr[0]["Name"] == "Metallic");
	CHECK(arr[0]["Type"] == "Float");
	CHECK(arr[0]["Value"] == 1.0f);
}

TEST_CASE("MaterialOverridesComponent loads a missing or non-array Overrides as empty", "[serialize][material]")
{
	for (const char* body : {R"({})", R"({"Overrides": 5})", R"({"Overrides": {"Name": "Roughness"}})", R"(null)"})
	{
		CAPTURE(body);
		World world;
		const auto j = EntityJson(std::string(R"({"Snowstorm::MaterialOverridesComponent": )") + body + "}");
		const Entity e = SceneSerializer::DeserializeEntity(world, j);
		REQUIRE(e);
		REQUIRE(e.HasComponent<MaterialOverridesComponent>());
		CHECK(e.GetComponent<MaterialOverridesComponent>().Overrides.empty());
	}
}

TEST_CASE("MaterialOverridesComponent keeps an unknown override key as a Float default", "[serialize][material]")
{
	World world;
	const auto j = EntityJson(R"({"Snowstorm::MaterialOverridesComponent": {"Overrides": [
		{"Name": "NoSuchProperty", "Type": "Hologram", "Value": 7.0}
	]}})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<MaterialOverridesComponent>());
	const auto& list = e.GetComponent<MaterialOverridesComponent>().Overrides;
	REQUIRE(list.size() == 1);
	CHECK(list[0].Name == "NoSuchProperty");
	CHECK(list[0].Type == MaterialOverrideType::Float);
	CHECK(list[0].Scalar == 7.0f);
}

TEST_CASE("MaterialOverridesComponent skips a wrong-typed value and keeps the entry's defaults", "[serialize][material]")
{
	World world;
	const auto j = EntityJson(R"({"Snowstorm::MaterialOverridesComponent": {"Overrides": [
		{"Name": "Roughness", "Type": "Float", "Value": "high"},
		{"Name": "BaseColor", "Type": "Color", "Value": 0.5},
		{"Name": "BaseColor", "Type": "Color", "Value": [1.0, 0.5, 0.25]},
		{"Name": "AlbedoTexture", "Type": "Texture", "Value": 12},
		{"Name": "Metallic", "Type": "Float"},
		{"Name": "AlbedoTexture", "Type": "Texture", "Value": "not a number"}
	]}})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<MaterialOverridesComponent>());
	const auto& list = e.GetComponent<MaterialOverridesComponent>().Overrides;
	REQUIRE(list.size() == 6);

	const MaterialOverride defaults;
	CHECK(list[0].Scalar == defaults.Scalar);
	CHECK(list[1].Color == defaults.Color);
	CHECK(list[2].Color == defaults.Color);
	CHECK(list[3].Texture == defaults.Texture);
	CHECK(list[4].Scalar == defaults.Scalar);
	CHECK(list[5].Texture == defaults.Texture);
}

TEST_CASE("MaterialOverridesComponent entry without Name or Type loads as an unnamed Float", "[serialize][material]")
{
	World world;
	const auto j = EntityJson(R"({"Snowstorm::MaterialOverridesComponent": {"Overrides": [{"Value": 2.0}]}})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	const auto& list = e.GetComponent<MaterialOverridesComponent>().Overrides;
	REQUIRE(list.size() == 1);
	CHECK(list[0].Name.empty());
	CHECK(list[0].Type == MaterialOverrideType::Float);
	CHECK(list[0].Scalar == 2.0f);
}

TEST_CASE("MaterialOverrideType string conversion round-trips and falls back to Float", "[serialize][material]")
{
	for (const auto type : {MaterialOverrideType::Float, MaterialOverrideType::Color, MaterialOverrideType::Texture})
	{
		CHECK(MaterialOverrideTypeFromString(MaterialOverrideTypeToString(type)) == type);
	}
	CHECK(MaterialOverrideTypeFromString("") == MaterialOverrideType::Float);
	CHECK(MaterialOverrideTypeFromString("color") == MaterialOverrideType::Float);
}

TEST_CASE("MeshComponent round-trips its mesh handle", "[serialize][mesh]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{4}, "Mesh");
	e.AddComponent<MeshComponent>().MeshHandle = AssetHandle{987654321098765ull};

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{4});
	REQUIRE(r);
	REQUIRE(r.HasComponent<MeshComponent>());
	CHECK(r.GetComponent<MeshComponent>().MeshHandle == AssetHandle{987654321098765ull});
	CHECK(r.GetComponent<MeshComponent>().MeshInstance == nullptr);
}

TEST_CASE("MaterialComponent round-trips its material handle", "[serialize][material]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{5}, "Mat");
	e.AddComponent<MaterialComponent>().Material = AssetHandle{555666777888999ull};

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{5});
	REQUIRE(r);
	REQUIRE(r.HasComponent<MaterialComponent>());
	CHECK(r.GetComponent<MaterialComponent>().Material == AssetHandle{555666777888999ull});
	CHECK(r.GetComponent<MaterialComponent>().MaterialInstance == nullptr);
}

TEST_CASE("Mesh and Material components read the legacy $asset key", "[serialize][mesh][material]")
{
	World world;
	const auto j = EntityJson(R"({
		"Snowstorm::MeshComponent": {"$asset": "111"},
		"Snowstorm::MaterialComponent": {"$asset": "222"}
	})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<MeshComponent>());
	REQUIRE(e.HasComponent<MaterialComponent>());
	CHECK(e.GetComponent<MeshComponent>().MeshHandle == AssetHandle{111});
	CHECK(e.GetComponent<MaterialComponent>().Material == AssetHandle{222});
}

TEST_CASE("Missing mesh and material handle keys load the zero handle", "[serialize][mesh][material]")
{
	World world;
	const auto j = EntityJson(R"({"Snowstorm::MeshComponent": {}, "Snowstorm::MaterialComponent": {}})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<MeshComponent>());
	REQUIRE(e.HasComponent<MaterialComponent>());
	CHECK(e.GetComponent<MeshComponent>().MeshHandle == AssetHandle{0});
	CHECK(e.GetComponent<MaterialComponent>().Material == AssetHandle{0});
}
