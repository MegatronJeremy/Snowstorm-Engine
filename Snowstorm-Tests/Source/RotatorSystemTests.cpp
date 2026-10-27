#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/Systems/RotatorSystem.hpp"
#include "Snowstorm/World/World.hpp"

#include <bit>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace Snowstorm;

namespace
{
	constexpr float kDt = 1.0f / 60.0f;

	entt::entity AddRotator(World& world, const glm::vec3& axis, const float speed, const glm::vec3& rotation)
	{
		auto& reg = world.GetRegistry();
		const entt::entity e = reg.create();
		reg.emplace<TransformComponent>(e).Rotation = rotation;
		reg.emplace<RotatorComponent>(e, axis, speed);
		return e;
	}

	glm::vec3 RotationOf(World& world, const entt::entity e)
	{
		return world.GetRegistry().Read<TransformComponent>(e).Rotation;
	}

	bool BitEqual(const glm::vec3& a, const glm::vec3& b)
	{
		return std::bit_cast<uint32_t>(a.x) == std::bit_cast<uint32_t>(b.x) &&
		       std::bit_cast<uint32_t>(a.y) == std::bit_cast<uint32_t>(b.y) &&
		       std::bit_cast<uint32_t>(a.z) == std::bit_cast<uint32_t>(b.z);
	}
}

TEST_CASE("RotatorSystem advances each rotator exactly as AdvanceRotation does", "[rotator]")
{
	World world;
	RotatorSystem system(&world);

	const glm::vec3 axes[] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {1, 2, 3}, {0, 5, 0}};
	std::vector<entt::entity> entities;
	std::vector<TransformComponent> expected;
	for (size_t i = 0; i < std::size(axes); ++i)
	{
		const float speed = 30.0f + 17.0f * static_cast<float>(i);
		const glm::vec3 start{0.1f * static_cast<float>(i), 0.3f, -0.2f};
		entities.push_back(AddRotator(world, axes[i], speed, start));

		TransformComponent tr;
		tr.Rotation = start;
		AdvanceRotation(tr, RotatorComponent{axes[i], speed}, kDt);
		expected.push_back(tr);
	}

	system.Apply(kDt, entt::null);

	for (size_t i = 0; i < entities.size(); ++i)
	{
		CHECK(BitEqual(RotationOf(world, entities[i]), expected[i].Rotation));
	}
}

TEST_CASE("RotatorSystem leaves speed 0, a zero axis and a transform-less rotator alone", "[rotator]")
{
	World world;
	RotatorSystem system(&world);

	const glm::vec3 start{0.4f, -0.7f, 0.2f};
	const entt::entity stopped = AddRotator(world, {0, 1, 0}, 0.0f, start);
	const entt::entity noAxis = AddRotator(world, {0, 0, 0}, 90.0f, start);

	auto& reg = world.GetRegistry();
	const entt::entity bare = reg.create();
	reg.emplace<RotatorComponent>(bare);

	system.Apply(kDt, entt::null);

	CHECK(BitEqual(RotationOf(world, stopped), start));
	CHECK(BitEqual(RotationOf(world, noAxis), start));
	CHECK_FALSE(reg.all_of<TransformComponent>(bare));
}

TEST_CASE("RotatorSystem axis and sign decide the direction of travel", "[rotator]")
{
	World world;
	RotatorSystem system(&world);

	const entt::entity yawPos = AddRotator(world, {0, 1, 0}, 90.0f, {0, 0, 0});
	const entt::entity yawNeg = AddRotator(world, {0, 1, 0}, -90.0f, {0, 0, 0});
	const entt::entity pitch = AddRotator(world, {1, 0, 0}, 90.0f, {0, 0, 0});

	system.Apply(0.1f, entt::null);

	CHECK(RotationOf(world, yawPos).y > 0.0f);
	CHECK(RotationOf(world, yawNeg).y == Catch::Approx(-RotationOf(world, yawPos).y).margin(1e-6));
	CHECK(RotationOf(world, yawPos).x == 0.0f);
	CHECK(RotationOf(world, pitch).x > 0.0f);
	CHECK(RotationOf(world, pitch).y == 0.0f);
}

TEST_CASE("RotatorSystem skips the entity held by the gizmo", "[rotator]")
{
	World world;
	RotatorSystem system(&world);

	const glm::vec3 start{0.1f, 0.2f, 0.3f};
	const entt::entity held = AddRotator(world, {0, 1, 0}, 60.0f, start);
	const entt::entity free = AddRotator(world, {0, 1, 0}, 60.0f, start);

	system.Apply(kDt, held);

	CHECK(BitEqual(RotationOf(world, held), start));
	CHECK_FALSE(BitEqual(RotationOf(world, free), start));
}

TEST_CASE("RotatorSystem accumulates over repeated frames", "[rotator]")
{
	World world;
	RotatorSystem system(&world);

	const entt::entity e = AddRotator(world, {0, 1, 0}, 90.0f, {0, 0, 0});
	TransformComponent reference;
	const RotatorComponent rot{{0, 1, 0}, 90.0f};

	for (int i = 0; i < 30; ++i)
	{
		system.Apply(kDt, entt::null);
		AdvanceRotation(reference, rot, kDt);
	}

	CHECK(BitEqual(RotationOf(world, e), reference.Rotation));
	CHECK(RotationOf(world, e).y == Catch::Approx(glm::radians(45.0f)).margin(1e-4));
}
