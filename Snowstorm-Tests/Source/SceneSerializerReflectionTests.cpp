#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
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
}

TEST_CASE("Asset-handle components round-trip through the reflected path", "[serialize]")
{
	World world;
	Entity e = world.CreateEntity("E");
	e.AddComponent<MeshComponent>().MeshHandle = AssetHandle{111};
	e.AddComponent<MaterialComponent>().Material = AssetHandle{222};
	e.AddComponent<CameraTargetComponent>().TargetViewportUUID = UUID{333};

	nlohmann::json j;
	REQUIRE(SceneSerializer::SerializeEntity(e, j));
	const auto& comps = j["Components"];
	CHECK(comps["Snowstorm::MeshComponent"]["Mesh"] == "111");
	CHECK(comps["Snowstorm::MaterialComponent"]["Material"] == "222");
	CHECK_FALSE(comps.contains("Snowstorm::IDComponent"));
	CHECK_FALSE(comps.contains("Snowstorm::TagComponent"));

	World other;
	const Entity r = SceneSerializer::DeserializeEntity(other, j);
	CHECK(r.GetComponent<MeshComponent>().MeshHandle == AssetHandle{111});
	CHECK(r.GetComponent<MaterialComponent>().Material == AssetHandle{222});
	CHECK(r.GetComponent<CameraTargetComponent>().TargetViewportUUID == UUID{333});
}

TEST_CASE("Scenes written with the legacy keys still load", "[serialize]")
{
	World world;
	const auto j = EntityJson(R"({
		"Snowstorm::MeshComponent": {"$asset": "7"},
		"Snowstorm::MaterialComponent": {"$asset": "0"},
		"Snowstorm::CameraTargetComponent": {"TargetViewport": "9"}
	})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<MeshComponent>());
	CHECK(e.GetComponent<MeshComponent>().MeshHandle == AssetHandle{7});
	REQUIRE(e.HasComponent<MaterialComponent>());
	CHECK(e.GetComponent<MaterialComponent>().Material == AssetHandle{0});
	CHECK(e.GetComponent<CameraTargetComponent>().TargetViewportUUID == UUID{9});
}

TEST_CASE("Material overrides keep their sparse array format", "[serialize]")
{
	World world;
	Entity e = world.CreateEntity("E");
	auto& mo = e.AddComponent<MaterialOverridesComponent>();
	MaterialOverride f;
	f.Name = "Roughness";
	f.Type = MaterialOverrideType::Float;
	f.Scalar = 0.5f;
	mo.Overrides.push_back(f);

	nlohmann::json j;
	REQUIRE(SceneSerializer::SerializeEntity(e, j));
	const auto& arr = j["Components"]["Snowstorm::MaterialOverridesComponent"]["Overrides"];
	REQUIRE(arr.size() == 1);
	CHECK(arr[0]["Name"] == "Roughness");
	CHECK(arr[0]["Type"] == "Float");
	CHECK(arr[0]["Value"] == 0.5f);

	World other;
	const Entity r = SceneSerializer::DeserializeEntity(other, j);
	const auto& back = r.GetComponent<MaterialOverridesComponent>().Overrides;
	REQUIRE(back.size() == 1);
	CHECK(back[0].Scalar == 0.5f);
}
