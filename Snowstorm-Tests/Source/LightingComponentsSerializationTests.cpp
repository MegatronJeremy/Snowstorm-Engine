#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Lighting/EnvironmentComponent.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
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

TEST_CASE("DirectionalLightComponent round-trips every reflected property", "[serialize][lighting]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Sun");
	auto& l = e.AddComponent<DirectionalLightComponent>();
	l.Enabled = false;
	l.Direction = {0.25f, -0.5f, 0.75f};
	l.Color = {0.9f, 0.5f, 0.125f};
	l.Intensity = 3.5f;
	l.CastShadows = false;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(r);
	REQUIRE(r.HasComponent<DirectionalLightComponent>());
	const auto& g = r.GetComponent<DirectionalLightComponent>();
	CHECK_FALSE(g.Enabled);
	CHECK(g.Direction == glm::vec3{0.25f, -0.5f, 0.75f});
	CHECK(g.Color == glm::vec3{0.9f, 0.5f, 0.125f});
	CHECK(g.Intensity == 3.5f);
	CHECK_FALSE(g.CastShadows);
}

TEST_CASE("PointLightComponent round-trips every reflected property", "[serialize][lighting]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{2}, "Bulb");
	auto& l = e.AddComponent<PointLightComponent>();
	l.Enabled = false;
	l.Color = {0.1f, 0.25f, 0.5f};
	l.Intensity = 7.25f;
	l.Range = 42.5f;
	l.CastShadows = false;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{2});
	REQUIRE(r);
	REQUIRE(r.HasComponent<PointLightComponent>());
	const auto& g = r.GetComponent<PointLightComponent>();
	CHECK_FALSE(g.Enabled);
	CHECK(g.Color == glm::vec3{0.1f, 0.25f, 0.5f});
	CHECK(g.Intensity == 7.25f);
	CHECK(g.Range == 42.5f);
	CHECK_FALSE(g.CastShadows);
}

TEST_CASE("SpotLightComponent round-trips every reflected property", "[serialize][lighting]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{3}, "Torch");
	auto& l = e.AddComponent<SpotLightComponent>();
	l.Enabled = false;
	l.Color = {0.75f, 0.25f, 0.5f};
	l.Intensity = 2.5f;
	l.Range = 18.0f;
	l.InnerAngleDeg = 12.5f;
	l.OuterAngleDeg = 44.5f;
	l.CastShadows = false;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{3});
	REQUIRE(r);
	REQUIRE(r.HasComponent<SpotLightComponent>());
	const auto& g = r.GetComponent<SpotLightComponent>();
	CHECK_FALSE(g.Enabled);
	CHECK(g.Color == glm::vec3{0.75f, 0.25f, 0.5f});
	CHECK(g.Intensity == 2.5f);
	CHECK(g.Range == 18.0f);
	CHECK(g.InnerAngleDeg == 12.5f);
	CHECK(g.OuterAngleDeg == 44.5f);
	CHECK_FALSE(g.CastShadows);
}

TEST_CASE("EnvironmentComponent round-trips every reflected property", "[serialize][lighting]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{4}, "Env");
	auto& c = e.AddComponent<EnvironmentComponent>();
	c.Background = BackgroundMode::SolidColor;
	c.SkyZenithColor = {0.25f, 0.5f, 0.75f};
	c.SkyHorizonColor = {0.125f, 0.375f, 0.625f};
	c.GroundColor = {0.5f, 0.25f, 0.0f};
	c.SkyIntensity = 2.5f;

	World loaded;
	Reload(world, loaded);
	const Entity r = loaded.FindEntityByUUID(UUID{4});
	REQUIRE(r);
	REQUIRE(r.HasComponent<EnvironmentComponent>());
	const auto& g = r.GetComponent<EnvironmentComponent>();
	CHECK(g.Background == BackgroundMode::SolidColor);
	CHECK(g.SkyZenithColor == glm::vec3{0.25f, 0.5f, 0.75f});
	CHECK(g.SkyHorizonColor == glm::vec3{0.125f, 0.375f, 0.625f});
	CHECK(g.GroundColor == glm::vec3{0.5f, 0.25f, 0.0f});
	CHECK(g.SkyIntensity == 2.5f);
}

TEST_CASE("Missing lighting keys load the component defaults", "[serialize][lighting]")
{
	World world;
	const auto j = EntityJson(R"({
		"Snowstorm::DirectionalLightComponent": {},
		"Snowstorm::PointLightComponent": {},
		"Snowstorm::SpotLightComponent": {},
		"Snowstorm::EnvironmentComponent": {}
	})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e);

	const PointLightComponent pointDefaults;
	REQUIRE(e.HasComponent<PointLightComponent>());
	const auto& p = e.GetComponent<PointLightComponent>();
	CHECK(p.Enabled == pointDefaults.Enabled);
	CHECK(p.Color == pointDefaults.Color);
	CHECK(p.Intensity == pointDefaults.Intensity);
	CHECK(p.Range == pointDefaults.Range);
	CHECK(p.CastShadows == pointDefaults.CastShadows);

	const SpotLightComponent spotDefaults;
	REQUIRE(e.HasComponent<SpotLightComponent>());
	const auto& s = e.GetComponent<SpotLightComponent>();
	CHECK(s.Enabled == spotDefaults.Enabled);
	CHECK(s.Color == spotDefaults.Color);
	CHECK(s.Intensity == spotDefaults.Intensity);
	CHECK(s.Range == spotDefaults.Range);
	CHECK(s.InnerAngleDeg == spotDefaults.InnerAngleDeg);
	CHECK(s.OuterAngleDeg == spotDefaults.OuterAngleDeg);
	CHECK(s.CastShadows == spotDefaults.CastShadows);

	const EnvironmentComponent envDefaults;
	REQUIRE(e.HasComponent<EnvironmentComponent>());
	const auto& env = e.GetComponent<EnvironmentComponent>();
	CHECK(env.Background == envDefaults.Background);
	CHECK(env.SkyZenithColor == envDefaults.SkyZenithColor);
	CHECK(env.SkyHorizonColor == envDefaults.SkyHorizonColor);
	CHECK(env.GroundColor == envDefaults.GroundColor);
	CHECK(env.SkyIntensity == envDefaults.SkyIntensity);

	// Direction and Color carry no default initializer, so only the keyed defaults are pinned.
	REQUIRE(e.HasComponent<DirectionalLightComponent>());
	const auto& d = e.GetComponent<DirectionalLightComponent>();
	CHECK(d.Enabled);
	CHECK(d.Intensity == 1.0f);
	CHECK(d.CastShadows);
}

TEST_CASE("A partial lighting block keeps unkeyed properties at their defaults", "[serialize][lighting]")
{
	World world;
	const auto j = EntityJson(R"({"Snowstorm::PointLightComponent": {"Range": 5.0}})");

	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<PointLightComponent>());
	const auto& p = e.GetComponent<PointLightComponent>();
	CHECK(p.Range == 5.0f);
	CHECK(p.Intensity == 1.0f);
	CHECK(p.Enabled);
	CHECK(p.CastShadows);
}
