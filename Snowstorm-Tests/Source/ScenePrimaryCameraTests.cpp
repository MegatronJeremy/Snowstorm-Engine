#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

TEST_CASE("FindScenePrimaryCamera returns the authored Primary camera only", "[camera]")
{
	World world;
	auto& reg = world.GetRegistry();
	constexpr entt::entity none = entt::null;
	CHECK(FindScenePrimaryCamera(reg) == none);

	// The editor's Scene-view camera never counts, even if its Primary flag is set
	Entity editorCam = world.CreateEntity("Editor Camera");
	editorCam.AddComponent<CameraComponent>().Primary = true;
	editorCam.AddComponent<DoNotSerializeComponent>();
	CHECK(FindScenePrimaryCamera(reg) == none);

	Entity secondary = world.CreateEntity("Secondary");
	secondary.AddComponent<CameraComponent>().Primary = false;
	CHECK(FindScenePrimaryCamera(reg) == none);

	Entity main = world.CreateEntity("Game Camera");
	main.AddComponent<CameraComponent>().Primary = true;
	CHECK(FindScenePrimaryCamera(reg) == main.Handle());
}
