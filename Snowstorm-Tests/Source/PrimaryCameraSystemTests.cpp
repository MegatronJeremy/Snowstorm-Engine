#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Systems/PrimaryCameraSystem.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

namespace
{
	entt::entity AddCamera(World& world, const bool primary)
	{
		auto& reg = world.GetRegistry();
		const entt::entity e = reg.create();
		reg.emplace<CameraComponent>(e).Primary = primary;
		return e;
	}

	bool IsPrimary(World& world, const entt::entity e)
	{
		return world.GetRegistry().Read<CameraComponent>(e).Primary;
	}

	int PrimaryCount(World& world)
	{
		int n = 0;
		for (const entt::entity e : world.GetRegistry().view<CameraComponent>())
		{
			n += IsPrimary(world, e) ? 1 : 0;
		}
		return n;
	}

	void RunFrame(World& world, PrimaryCameraSystem& system)
	{
		system.Execute(Timestep{});
		world.GetRegistry().ClearTrackedComponents();
	}
}

TEST_CASE("PrimaryCameraSystem leaves zero or one Primary camera alone", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);

	RunFrame(world, system);
	CHECK(PrimaryCount(world) == 0);

	const entt::entity off = AddCamera(world, false);
	RunFrame(world, system);
	CHECK_FALSE(IsPrimary(world, off));

	const entt::entity main = AddCamera(world, true);
	RunFrame(world, system);
	CHECK(IsPrimary(world, main));
	CHECK_FALSE(IsPrimary(world, off));
}

TEST_CASE("PrimaryCameraSystem demotes all but one of two unchanged Primary cameras", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);
	const entt::entity first = AddCamera(world, true);
	const entt::entity second = AddCamera(world, true);
	world.GetRegistry().ClearTrackedComponents();

	RunFrame(world, system);
	CHECK(PrimaryCount(world) == 1);
	CHECK(IsPrimary(world, first) != IsPrimary(world, second));
}

TEST_CASE("PrimaryCameraSystem prefers the camera that changed this frame", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);
	const entt::entity first = AddCamera(world, true);
	const entt::entity second = AddCamera(world, false);
	RunFrame(world, system);
	REQUIRE(IsPrimary(world, first));

	// "Set as Primary" on the second camera: the fresh intent wins over the earlier holder
	world.GetRegistry().patch<CameraComponent>(second, [](CameraComponent& c)
	                                           { c.Primary = true; });
	RunFrame(world, system);
	CHECK(IsPrimary(world, second));
	CHECK_FALSE(IsPrimary(world, first));
}

TEST_CASE("PrimaryCameraSystem converges in one frame and is stable afterwards", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);
	const entt::entity a = AddCamera(world, true);
	const entt::entity b = AddCamera(world, true);
	const entt::entity c = AddCamera(world, true);
	world.GetRegistry().ClearTrackedComponents();

	RunFrame(world, system);
	REQUIRE(PrimaryCount(world) == 1);
	const bool aWon = IsPrimary(world, a);
	const bool bWon = IsPrimary(world, b);
	const bool cWon = IsPrimary(world, c);

	for (int i = 0; i < 3; ++i)
	{
		RunFrame(world, system);
		CHECK(IsPrimary(world, a) == aWon);
		CHECK(IsPrimary(world, b) == bWon);
		CHECK(IsPrimary(world, c) == cWon);
	}
}

TEST_CASE("PrimaryCameraSystem picks the same winner on every identically built world", "[camera]")
{
	auto winnerIndex = []
	{
		World world;
		PrimaryCameraSystem system(&world);
		entt::entity cams[4];
		for (auto& cam : cams)
		{
			cam = AddCamera(world, true);
		}
		world.GetRegistry().ClearTrackedComponents();
		RunFrame(world, system);
		int index = -1;
		for (int i = 0; i < 4; ++i)
		{
			if (IsPrimary(world, cams[i]))
			{
				REQUIRE(index == -1);
				index = i;
			}
		}
		return index;
	};

	const int expected = winnerIndex();
	REQUIRE(expected >= 0);
	for (int run = 0; run < 5; ++run)
	{
		CHECK(winnerIndex() == expected);
	}
}

TEST_CASE("PrimaryCameraSystem releases the Primary slot when the flag is cleared", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);
	const entt::entity a = AddCamera(world, true);
	const entt::entity b = AddCamera(world, false);
	RunFrame(world, system);

	world.GetRegistry().patch<CameraComponent>(a, [](CameraComponent& c)
	                                           { c.Primary = false; });
	RunFrame(world, system);
	CHECK(PrimaryCount(world) == 0);

	world.GetRegistry().patch<CameraComponent>(b, [](CameraComponent& c)
	                                           { c.Primary = true; });
	RunFrame(world, system);
	CHECK(IsPrimary(world, b));
	CHECK_FALSE(IsPrimary(world, a));
}

TEST_CASE("PrimaryCameraSystem ignores DoNotSerialize cameras", "[camera]")
{
	World world;
	PrimaryCameraSystem system(&world);
	const entt::entity editorCam = AddCamera(world, true);
	world.GetRegistry().emplace<DoNotSerializeComponent>(editorCam);
	const entt::entity game = AddCamera(world, true);
	world.GetRegistry().ClearTrackedComponents();

	RunFrame(world, system);
	CHECK(IsPrimary(world, editorCam));
	CHECK(IsPrimary(world, game));
}
