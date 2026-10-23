#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/NativeScriptComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <nlohmann/json.hpp>

#include <rttr/type>

#include <algorithm>
#include <string>
#include <vector>

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

	template <typename T>
	std::vector<std::string> PropertyNames()
	{
		const rttr::type type = rttr::type::get<T>();
		REQUIRE(type.is_valid());
		std::vector<std::string> names;
		for (const auto& p : type.get_properties())
		{
			names.push_back(p.get_name().to_string());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	// The serialized "Components" block of the single entity in `world`.
	nlohmann::json ComponentBlock(const World& world, const char* typeName)
	{
		const nlohmann::json scene = nlohmann::json::parse(SceneSerializer::SerializeToString(world));
		const auto entities = scene.find("Entities");
		REQUIRE(entities != scene.end());
		REQUIRE(entities->is_array());
		REQUIRE(entities->size() == 1);
		const auto& comps = (*entities)[0].at("Components");
		REQUIRE(comps.contains(typeName));
		return comps.at(typeName);
	}
}

TEST_CASE("VisibilityComponent and CameraVisibilityComponent reflect exactly Mask", "[serialize][visibility]")
{
	CHECK(PropertyNames<VisibilityComponent>() == std::vector<std::string>{"Mask"});
	CHECK(PropertyNames<CameraVisibilityComponent>() == std::vector<std::string>{"Mask"});
}

TEST_CASE("Visibility masks round-trip non-default values", "[serialize][visibility]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Vis");
	e.AddComponent<VisibilityComponent>().Mask = Visibility::Game | Visibility::MaterialPreview;
	e.AddComponent<CameraVisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(r);
	REQUIRE(r.HasComponent<VisibilityComponent>());
	REQUIRE(r.HasComponent<CameraVisibilityComponent>());
	CHECK(r.GetComponent<VisibilityComponent>().Mask == (Visibility::Game | Visibility::MaterialPreview));
	CHECK(r.GetComponent<CameraVisibilityComponent>().Mask == (Visibility::Scene | Visibility::Game));
}

TEST_CASE("Visibility masks keep the extremes: none and all bits", "[serialize][visibility]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{2}, "Vis");
	e.AddComponent<VisibilityComponent>().Mask = Visibility::All;
	e.AddComponent<CameraVisibilityComponent>().Mask = Visibility::None;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{2});
	REQUIRE(r);
	CHECK(r.GetComponent<VisibilityComponent>().Mask == Visibility::All);
	CHECK(r.GetComponent<CameraVisibilityComponent>().Mask == Visibility::None);
}

TEST_CASE("A missing visibility Mask loads each component's own default", "[serialize][visibility]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(
	    world, EntityJson(R"({"Snowstorm::VisibilityComponent": {}, "Snowstorm::CameraVisibilityComponent": {}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<VisibilityComponent>());
	REQUIRE(e.HasComponent<CameraVisibilityComponent>());
	CHECK(e.GetComponent<VisibilityComponent>().Mask == VisibilityComponent{}.Mask);
	CHECK(e.GetComponent<CameraVisibilityComponent>().Mask == CameraVisibilityComponent{}.Mask);
}

TEST_CASE("ViewportInteractionComponent reflects Hovered and Focused read-only", "[serialize][viewportinteraction]")
{
	CHECK(PropertyNames<ViewportInteractionComponent>() == std::vector<std::string>{"Focused", "Hovered"});
	const rttr::type type = rttr::type::get<ViewportInteractionComponent>();
	CHECK(type.get_property("Hovered").is_readonly());
	CHECK(type.get_property("Focused").is_readonly());
}

TEST_CASE("ViewportInteractionComponent state is written but never restored on load",
          "[serialize][viewportinteraction]")
{
	// Both flags are per-frame input state. They are registered read-only, so a load cannot set them:
	// a scene saved while hovered reopens with the component present and both flags false.
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{3}, "View");
	auto& v = e.AddComponent<ViewportInteractionComponent>();
	v.Hovered = true;
	v.Focused = true;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{3});
	REQUIRE(r);
	REQUIRE(r.HasComponent<ViewportInteractionComponent>());
	CHECK(r.GetComponent<ViewportInteractionComponent>() == ViewportInteractionComponent{});

	World fromJson;
	const Entity f = SceneSerializer::DeserializeEntity(
	    fromJson, EntityJson(R"({"Snowstorm::ViewportInteractionComponent": {"Hovered": true, "Focused": true}})"));
	REQUIRE(f);
	REQUIRE(f.HasComponent<ViewportInteractionComponent>());
	CHECK(f.GetComponent<ViewportInteractionComponent>() == ViewportInteractionComponent{});
}

TEST_CASE("NativeScriptComponent reflects only Instance, never the bind callbacks", "[serialize][nativescript]")
{
	CHECK(PropertyNames<NativeScriptComponent>() == std::vector<std::string>{"Instance"});
}

TEST_CASE("NativeScriptComponent never writes a live instance pointer into the scene", "[serialize][nativescript]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{4}, "Script");
	auto& s = e.AddComponent<NativeScriptComponent>();
	s.Bind<ScriptableEntity>();
	s.InstantiateScript();
	REQUIRE(s.Instance != nullptr);

	const nlohmann::json block = ComponentBlock(world, "Snowstorm::NativeScriptComponent");
	if (block.contains("Instance"))
	{
		CHECK(block.at("Instance").is_null());
	}
	for (const auto& [key, value] : block.items())
	{
		CHECK(key == "Instance");
		CHECK(value.is_null());
	}

	s.DestroyScript();
	CHECK(s.Instance == nullptr);
}

TEST_CASE("NativeScriptComponent loads unbound with a null instance", "[serialize][nativescript]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{5}, "Script");
	auto& s = e.AddComponent<NativeScriptComponent>();
	s.Bind<ScriptableEntity>();
	s.InstantiateScript();

	World loaded;
	Reload(world, loaded);
	s.DestroyScript();

	const Entity r = loaded.FindEntityByUUID(UUID{5});
	REQUIRE(r);
	REQUIRE(r.HasComponent<NativeScriptComponent>());
	const auto& g = r.GetComponent<NativeScriptComponent>();
	CHECK(g.Instance == nullptr);
	CHECK_FALSE(static_cast<bool>(g.InstantiateScript));
	CHECK_FALSE(static_cast<bool>(g.DestroyScript));
}

TEST_CASE("A hand-written Instance value in a scene cannot plant a pointer", "[serialize][nativescript]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(
	    world, EntityJson(R"({"Snowstorm::NativeScriptComponent": {"Instance": 305441741}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<NativeScriptComponent>());
	CHECK(e.GetComponent<NativeScriptComponent>().Instance == nullptr);
}
