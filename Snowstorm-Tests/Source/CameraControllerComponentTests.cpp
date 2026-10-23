#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <rttr/type>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

using namespace Snowstorm;

namespace
{
	std::optional<float> Meta(const std::string& property, const char* key)
	{
		const rttr::type type = rttr::type::get<CameraControllerComponent>();
		REQUIRE(type.is_valid());
		const rttr::property prop = type.get_property(property);
		REQUIRE(prop.is_valid());
		const rttr::variant v = prop.get_metadata(key);
		if (!v.is_valid() || !v.can_convert<float>())
			return std::nullopt;
		return v.get_value<float>();
	}

	void Reload(const World& world, World& loaded)
	{
		REQUIRE(SceneSerializer::DeserializeFromString(loaded, SceneSerializer::SerializeToString(world)));
	}

	void CheckEqual(const CameraControllerComponent& a, const CameraControllerComponent& b)
	{
		CHECK(a.RotationEnabled == b.RotationEnabled);
		CHECK(a.ZoomSpeed == b.ZoomSpeed);
		CHECK(a.MoveSpeed == b.MoveSpeed);
		CHECK(a.LookSensitivity == b.LookSensitivity);
		CHECK(a.SprintMultiplier == b.SprintMultiplier);
		CHECK(a.SlowMultiplier == b.SlowMultiplier);
		CHECK(a.SpeedAdjustStep == b.SpeedAdjustStep);
		CHECK(a.MinMoveSpeed == b.MinMoveSpeed);
		CHECK(a.MaxMoveSpeed == b.MaxMoveSpeed);
		CHECK(a.LookSmoothing == b.LookSmoothing);
		CHECK(a.MoveSmoothing == b.MoveSmoothing);
		CHECK(a.ZoomSmoothing == b.ZoomSmoothing);
	}

	CameraControllerComponent RoundTrip(const CameraControllerComponent& in)
	{
		World world;
		Entity e = world.CreateEntityWithUUID(UUID{7}, "Cam");
		e.AddComponent<CameraControllerComponent>() = in;

		World loaded;
		Reload(world, loaded);
		const Entity r = loaded.FindEntityByUUID(UUID{7});
		REQUIRE(r);
		REQUIRE(r.HasComponent<CameraControllerComponent>());
		return r.GetComponent<CameraControllerComponent>();
	}
}

TEST_CASE("CameraControllerComponent reflects all 12 properties", "[camera][reflection]")
{
	const rttr::type type = rttr::type::get<CameraControllerComponent>();
	REQUIRE(type.is_valid());
	CHECK(type.get_properties().size() == 12);
	for (const char* name : {"LookSensitivity", "MoveSpeed", "RotationEnabled", "ZoomSpeed", "SprintMultiplier", "SlowMultiplier", "SpeedAdjustStep", "MinMoveSpeed", "MaxMoveSpeed", "LookSmoothing", "MoveSmoothing", "ZoomSmoothing"})
		CHECK(type.get_property(name).is_valid());
}

TEST_CASE("CameraControllerComponent properties carry the documented Min/Max bounds", "[camera][reflection]")
{
	// Multipliers scale MoveSpeed, so a negative one would invert movement; a step below 1 would shrink
	// fly speed on scroll-up; negative smoothing rates are invalid.
	CHECK(Meta("LookSensitivity", "Min") == 0.0f);
	CHECK(Meta("MoveSpeed", "Min") == 0.0f);
	CHECK(Meta("ZoomSpeed", "Min") == 0.0f);
	CHECK(Meta("SprintMultiplier", "Min") == 1.0f);
	CHECK(Meta("SlowMultiplier", "Min") == 0.0f);
	CHECK(Meta("SlowMultiplier", "Max") == 1.0f);
	CHECK(Meta("SpeedAdjustStep", "Min") == 1.0f);
	CHECK(Meta("MinMoveSpeed", "Min") == 0.0f);
	CHECK(Meta("MaxMoveSpeed", "Min") == 0.0f);
	CHECK(Meta("LookSmoothing", "Min") == 0.0f);
	CHECK(Meta("MoveSmoothing", "Min") == 0.0f);
	CHECK(Meta("ZoomSmoothing", "Min") == 0.0f);

	for (const char* name : {"LookSensitivity", "MoveSpeed", "ZoomSpeed", "SprintMultiplier", "SpeedAdjustStep", "MinMoveSpeed", "MaxMoveSpeed", "LookSmoothing", "MoveSmoothing", "ZoomSmoothing"})
		CHECK_FALSE(Meta(name, "Max").has_value());
	CHECK(Meta("RotationEnabled", "Min") == std::nullopt);
}

TEST_CASE("CameraControllerComponent drag speeds are the documented steps", "[camera][reflection]")
{
	CHECK(Meta("LookSensitivity", "Speed") == 0.01f);
	CHECK(Meta("SprintMultiplier", "Speed") == 0.05f);
	CHECK(Meta("SlowMultiplier", "Speed") == 0.01f);
	CHECK(Meta("SpeedAdjustStep", "Speed") == 0.01f);
}

TEST_CASE("CameraControllerComponent round-trips every non-default property", "[camera][serialize]")
{
	CameraControllerComponent in;
	in.RotationEnabled = false;
	in.ZoomSpeed = 3.5f;
	in.MoveSpeed = 12.25f;
	in.LookSensitivity = 0.375f;
	in.SprintMultiplier = 6.5f;
	in.SlowMultiplier = 0.125f;
	in.SpeedAdjustStep = 1.5f;
	in.MinMoveSpeed = 0.5f;
	in.MaxMoveSpeed = 250.0f;
	in.LookSmoothing = 0.0f;
	in.MoveSmoothing = 7.5f;
	in.ZoomSmoothing = 20.25f;

	CheckEqual(RoundTrip(in), in);
}

TEST_CASE("CameraControllerComponent defaults round-trip unchanged", "[camera][serialize]")
{
	const CameraControllerComponent defaults;
	CheckEqual(RoundTrip(defaults), defaults);
}

TEST_CASE("A partial CameraControllerComponent block keeps unkeyed properties at their defaults", "[camera][serialize]")
{
	World world;
	const auto j = nlohmann::json::parse(R"({"UUID":"42","Name":"E","Components":{"Snowstorm::CameraControllerComponent":{"MoveSpeed":9.0}}})");
	const Entity e = SceneSerializer::DeserializeEntity(world, j);
	REQUIRE(e.HasComponent<CameraControllerComponent>());

	CameraControllerComponent expected;
	expected.MoveSpeed = 9.0f;
	CheckEqual(e.GetComponent<CameraControllerComponent>(), expected);
}
