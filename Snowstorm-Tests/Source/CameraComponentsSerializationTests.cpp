#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
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

TEST_CASE("CameraComponent round-trips every reflected property", "[serialize][camera]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Cam");
	auto& c = e.AddComponent<CameraComponent>();
	c.Projection = CameraComponent::ProjectionType::Orthographic;
	c.RenderPath = CameraComponent::RenderPathType::Canvas2D;
	c.BackgroundColor = {0.25f, 0.5f, 0.75f, 0.125f};
	c.PerspectiveFOV = 1.25f;
	c.PerspectiveNear = 0.5f;
	c.PerspectiveFar = 250.0f;
	c.OrthographicSize = 6.5f;
	c.OrthographicNear = -3.0f;
	c.OrthographicFar = 42.0f;
	c.Primary = false;
	c.FixedAspectRatio = true;
	c.AspectRatio = 1.5f;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(r);
	REQUIRE(r.HasComponent<CameraComponent>());
	const auto& g = r.GetComponent<CameraComponent>();
	CHECK(g.Projection == CameraComponent::ProjectionType::Orthographic);
	CHECK(g.RenderPath == CameraComponent::RenderPathType::Canvas2D);
	CHECK(g.BackgroundColor == glm::vec4{0.25f, 0.5f, 0.75f, 0.125f});
	CHECK(g.PerspectiveFOV == 1.25f);
	CHECK(g.PerspectiveNear == 0.5f);
	CHECK(g.PerspectiveFar == 250.0f);
	CHECK(g.OrthographicSize == 6.5f);
	CHECK(g.OrthographicNear == -3.0f);
	CHECK(g.OrthographicFar == 42.0f);
	CHECK_FALSE(g.Primary);
	CHECK(g.FixedAspectRatio);
	CHECK(g.AspectRatio == 1.5f);
}

TEST_CASE("Missing camera keys load the component defaults", "[serialize][camera]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(world, EntityJson(R"({"Snowstorm::CameraComponent": {}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<CameraComponent>());

	const CameraComponent d;
	const auto& g = e.GetComponent<CameraComponent>();
	CHECK(g.Projection == d.Projection);
	CHECK(g.RenderPath == d.RenderPath);
	CHECK(g.BackgroundColor == d.BackgroundColor);
	CHECK(g.PerspectiveFOV == d.PerspectiveFOV);
	CHECK(g.PerspectiveNear == d.PerspectiveNear);
	CHECK(g.PerspectiveFar == d.PerspectiveFar);
	CHECK(g.OrthographicSize == d.OrthographicSize);
	CHECK(g.OrthographicNear == d.OrthographicNear);
	CHECK(g.OrthographicFar == d.OrthographicFar);
	CHECK(g.Primary == d.Primary);
	CHECK(g.FixedAspectRatio == d.FixedAspectRatio);
	CHECK(g.AspectRatio == d.AspectRatio);
}

TEST_CASE("CameraTargetComponent round-trips the target UUID and not the runtime cache", "[serialize][camera]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{2}, "Cam");
	auto& t = e.AddComponent<CameraTargetComponent>();
	t.TargetViewportUUID = UUID{0xABCDEF123ull};
	t.TargetViewportEntity = static_cast<entt::entity>(7);

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{2});
	REQUIRE(r);
	REQUIRE(r.HasComponent<CameraTargetComponent>());
	const auto& g = r.GetComponent<CameraTargetComponent>();
	CHECK(g.TargetViewportUUID == UUID{0xABCDEF123ull});
	CHECK(g.TargetViewportEntity == entt::null);
}

TEST_CASE("CameraTargetComponent loads the legacy TargetViewport key", "[serialize][camera]")
{
	World world;
	const UUID target{0x1234567ull};
	nlohmann::json block;
	block["TargetViewport"] = target.ToString();
	nlohmann::json components;
	components["Snowstorm::CameraTargetComponent"] = block;

	const Entity e = SceneSerializer::DeserializeEntity(world, EntityJson(components.dump()));
	REQUIRE(e);
	REQUIRE(e.HasComponent<CameraTargetComponent>());
	CHECK(e.GetComponent<CameraTargetComponent>().TargetViewportUUID == target);
}

TEST_CASE("ViewportComponent round-trips Size", "[serialize][camera]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{3}, "View");
	e.AddComponent<ViewportComponent>().Size = {1280.0f, 720.5f};

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{3});
	REQUIRE(r);
	REQUIRE(r.HasComponent<ViewportComponent>());
	CHECK(r.GetComponent<ViewportComponent>().Size == glm::vec2{1280.0f, 720.5f});
}
