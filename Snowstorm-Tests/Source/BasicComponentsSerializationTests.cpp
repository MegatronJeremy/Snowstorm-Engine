#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/AudioListenerComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <nlohmann/json.hpp>
#include <rttr/type>

#include <cstdint>
#include <string>

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
	bool HasProperty(const std::string& name)
	{
		const rttr::type type = rttr::type::get<T>();
		return type.is_valid() && type.get_property(name).is_valid();
	}

	const ComponentInfo* FindInfo(const char* typeName)
	{
		for (const auto& info : GetComponentRegistry())
		{
			if (info.Type.is_valid() && info.Type.get_name().to_string() == typeName)
			{
				return &info;
			}
		}
		return nullptr;
	}
}

TEST_CASE("Rotator, AudioListener, Tag and ID register their reflected properties", "[serialize][basic]")
{
	CHECK(HasProperty<RotatorComponent>("Axis"));
	CHECK(HasProperty<RotatorComponent>("SpeedDegPerSec"));
	CHECK(HasProperty<AudioListenerComponent>("Enabled"));
	CHECK(HasProperty<TagComponent>("Tag"));
	CHECK(HasProperty<IDComponent>("Id"));
}

TEST_CASE("Tag and ID are structural, Rotator and AudioListener are not", "[serialize][basic]")
{
	const ComponentInfo* tag = FindInfo("Snowstorm::TagComponent");
	const ComponentInfo* idInfo = FindInfo("Snowstorm::IDComponent");
	const ComponentInfo* rotator = FindInfo("Snowstorm::RotatorComponent");
	const ComponentInfo* listener = FindInfo("AudioListenerComponent");
	REQUIRE(tag);
	REQUIRE(idInfo);
	REQUIRE(rotator);
	REQUIRE(listener);
	CHECK(tag->Structural);
	CHECK(idInfo->Structural);
	CHECK_FALSE(rotator->Structural);
	CHECK_FALSE(listener->Structural);
}

TEST_CASE("RotatorComponent round-trips Axis and SpeedDegPerSec without normalizing", "[serialize][basic]")
{
	World world;
	Entity e = world.CreateEntityWithUUID(UUID{1}, "Spin");
	auto& r = e.AddComponent<RotatorComponent>();
	r.Axis = {0.5f, -2.0f, 4.0f};
	r.SpeedDegPerSec = -135.5f;

	World loaded;
	Reload(world, loaded);
	const Entity l = loaded.FindEntityByUUID(UUID{1});
	REQUIRE(l);
	REQUIRE(l.HasComponent<RotatorComponent>());
	CHECK(l.GetComponent<RotatorComponent>().Axis == glm::vec3{0.5f, -2.0f, 4.0f});
	CHECK(l.GetComponent<RotatorComponent>().SpeedDegPerSec == -135.5f);
}

TEST_CASE("Missing rotator keys load the component defaults", "[serialize][basic]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(world, EntityJson(R"({"Snowstorm::RotatorComponent": {}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<RotatorComponent>());
	const RotatorComponent d{};
	CHECK(e.GetComponent<RotatorComponent>().Axis == d.Axis);
	CHECK(e.GetComponent<RotatorComponent>().SpeedDegPerSec == d.SpeedDegPerSec);
}

TEST_CASE("AudioListenerComponent round-trips Enabled in both states", "[serialize][basic]")
{
	World world;
	world.CreateEntityWithUUID(UUID{2}, "Off").AddComponent<AudioListenerComponent>().Enabled = false;
	world.CreateEntityWithUUID(UUID{3}, "On").AddComponent<AudioListenerComponent>().Enabled = true;

	World loaded;
	Reload(world, loaded);
	const Entity off = loaded.FindEntityByUUID(UUID{2});
	const Entity on = loaded.FindEntityByUUID(UUID{3});
	REQUIRE(off);
	REQUIRE(on);
	REQUIRE(off.HasComponent<AudioListenerComponent>());
	REQUIRE(on.HasComponent<AudioListenerComponent>());
	CHECK_FALSE(off.GetComponent<AudioListenerComponent>().Enabled);
	CHECK(on.GetComponent<AudioListenerComponent>().Enabled);
}

TEST_CASE("An empty AudioListener block loads enabled", "[serialize][basic]")
{
	World world;
	const Entity e = SceneSerializer::DeserializeEntity(world, EntityJson(R"({"AudioListenerComponent": {}})"));
	REQUIRE(e);
	REQUIRE(e.HasComponent<AudioListenerComponent>());
	CHECK(e.GetComponent<AudioListenerComponent>().Enabled);
}

TEST_CASE("TagComponent round-trips non-ASCII names and defaults an empty name", "[serialize][basic]")
{
	const std::string names[] = {"", "\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0\xD1\x87 \xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC", "  spaced \"quoted\" \\ name\t"};

	World world;
	uint64_t nextId = 10;
	for (const auto& n : names)
	{
		world.CreateEntityWithUUID(UUID{nextId++}, n);
	}

	const nlohmann::json doc = nlohmann::json::parse(SceneSerializer::SerializeToString(world));
	CHECK(doc.dump().find("TagComponent") == std::string::npos);

	World loaded;
	Reload(world, loaded);
	nextId = 10;
	for (const auto& n : names)
	{
		const Entity l = loaded.FindEntityByUUID(UUID{nextId++});
		REQUIRE(l);
		REQUIRE(l.HasComponent<TagComponent>());
		CHECK(l.GetComponent<TagComponent>().Tag == (n.empty() ? std::string("Entity") : n));
	}
}

TEST_CASE("IDComponent UUID survives save and load in the entity header", "[serialize][basic]")
{
	const uint64_t bigValue = 0xFEDCBA9876543210ull;

	World world;
	world.CreateEntityWithUUID(UUID{bigValue}, "Big");

	const nlohmann::json doc = nlohmann::json::parse(SceneSerializer::SerializeToString(world));
	CHECK(doc.dump().find("IDComponent") == std::string::npos);

	World loaded;
	Reload(world, loaded);
	const Entity l = loaded.FindEntityByUUID(UUID{bigValue});
	REQUIRE(l);
	REQUIRE(l.HasComponent<IDComponent>());
	CHECK(l.GetComponent<IDComponent>().Id.Value() == bigValue);
}
