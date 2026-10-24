#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/World/World.hpp"

#include <cstdint>

using Catch::Approx;
using namespace Snowstorm;

// RotatorSystem::Execute reads the renderer's frame counter through the Application, so it cannot run
// headless. This system replays its apply step (the same ParallelForEach declaration and the same
// RotatorSkipsEntity/AdvanceRotation body) over a World; the clock decision is pinned through
// RotatorTickDue. Wiring drift between the two is not caught. No JobSystem is registered, so
// ParallelForEach takes its serial path; serial-vs-pool equality is RotatorMathTests.

namespace
{
	class ApplyRotatorsSystem final : public System
	{
	public:
		using System::System;

		void Execute(Timestep) override {}

		void Apply(const float dt, const entt::entity gizmoHeld = entt::null)
		{
			ParallelForEach<Write<TransformComponent>, Read<RotatorComponent>>(
			    [dt, gizmoHeld](const entt::entity e, TransformComponent& tr, const RotatorComponent& rot)
			    {
				    if (RotatorSkipsEntity(e, gizmoHeld))
				    {
					    return;
				    }
				    AdvanceRotation(tr, rot, dt);
			    });
		}
	};

	entt::entity AddRotator(World& world, const glm::vec3& axis, const float speedDegPerSec, const glm::vec3& rotation = {})
	{
		auto& reg = world.GetRegistry();
		const entt::entity e = reg.create();
		reg.emplace<TransformComponent>(e).Rotation = rotation;
		reg.emplace<RotatorComponent>(e, RotatorComponent{axis, speedDegPerSec});
		return e;
	}

	glm::vec3 RotationOf(World& world, const entt::entity e)
	{
		return world.GetRegistry().get<TransformComponent>(e).Rotation;
	}
}

TEST_CASE("RotatorSystem apply step matches AdvanceRotation per entity", "[rotator][ecs]")
{
	World world;
	ApplyRotatorsSystem system(&world);
	constexpr float dt = 1.0f / 60.0f;

	const glm::vec3 start{0.3f, 1.1f, -0.4f};
	const entt::entity a = AddRotator(world, {0, 1, 0}, 90.0f, start);
	const entt::entity b = AddRotator(world, {1, 2, 3}, -45.0f, start);

	TransformComponent expectA, expectB;
	expectA.Rotation = expectB.Rotation = start;
	for (int i = 0; i < 5; ++i)
	{
		system.Apply(dt);
		AdvanceRotation(expectA, RotatorComponent{{0, 1, 0}, 90.0f}, dt);
		AdvanceRotation(expectB, RotatorComponent{{1, 2, 3}, -45.0f}, dt);
	}

	CHECK(RotationOf(world, a) == expectA.Rotation);
	CHECK(RotationOf(world, b) == expectB.Rotation);
	CHECK(RotationOf(world, a) != start);
}

TEST_CASE("RotatorSystem leaves speed 0 and zero-axis rotators unchanged", "[rotator][ecs]")
{
	World world;
	ApplyRotatorsSystem system(&world);
	const glm::vec3 start{0.5f, -0.2f, 0.9f};
	const entt::entity still = AddRotator(world, {0, 1, 0}, 0.0f, start);
	const entt::entity noAxis = AddRotator(world, {0, 0, 0}, 90.0f, start);
	const entt::entity spinning = AddRotator(world, {0, 1, 0}, 90.0f, start);

	system.Apply(0.1f);

	CHECK(RotationOf(world, still) == start);
	CHECK(RotationOf(world, noAxis) == start);
	CHECK(RotationOf(world, spinning) != start);
}

TEST_CASE("RotatorSystem accumulates the rotation about the axis", "[rotator][ecs]")
{
	World world;
	ApplyRotatorsSystem system(&world);
	const entt::entity e = AddRotator(world, {0, 2, 0}, 90.0f); // unnormalised axis

	for (int i = 0; i < 10; ++i)
	{
		system.Apply(0.1f); // 10 x 9 degrees
	}

	const glm::vec3 r = RotationOf(world, e);
	CHECK(r.y == Approx(glm::radians(90.0f)).margin(1e-5));
	CHECK(r.x == Approx(0.0f).margin(1e-5));
	CHECK(r.z == Approx(0.0f).margin(1e-5));
}

TEST_CASE("RotatorSystem skips the entity the gizmo holds", "[rotator][ecs]")
{
	World world;
	ApplyRotatorsSystem system(&world);
	const entt::entity held = AddRotator(world, {0, 1, 0}, 90.0f);
	const entt::entity free = AddRotator(world, {0, 1, 0}, 90.0f);

	system.Apply(0.1f, held);

	CHECK(RotationOf(world, held) == glm::vec3(0.0f));
	CHECK(RotationOf(world, free) != glm::vec3(0.0f));

	system.Apply(0.1f); // gizmo released
	CHECK(RotationOf(world, held) != glm::vec3(0.0f));
}

TEST_CASE("RotatorTickDue advances once per clock value when fixed-step or route-driven", "[rotator]")
{
	constexpr uint64_t first = UINT64_MAX;
	CHECK(RotatorTickDue(true, false, 0, first));
	CHECK(RotatorTickDue(false, true, 0, first));
	CHECK_FALSE(RotatorTickDue(true, false, 7, 7));
	CHECK_FALSE(RotatorTickDue(false, true, 7, 7));
	CHECK(RotatorTickDue(true, false, 8, 7));
	// Free-running: every call advances, even on a repeated clock value.
	CHECK(RotatorTickDue(false, false, 7, 7));
}
