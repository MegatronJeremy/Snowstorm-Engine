#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/PrevTransformComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Systems/PrevTransformSnapshotSystem.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

TEST_CASE("Snapshot holds the previous frame's model matrix across a transform change", "[ecs][motion]")
{
	World world;
	auto& reg = world.GetRegistry();
	const auto e = reg.create();
	reg.emplace<MeshComponent>(e);
	auto& tr = reg.emplace<TransformComponent>(e);
	tr.Position = {1.0f, 2.0f, 3.0f};
	const glm::mat4 frame0 = tr.GetTransformMatrix();

	PrevTransformSnapshotSystem system(&world);

	// First frame the object exists: Prev == current, so velocity is zero.
	system.Execute(Timestep{});
	REQUIRE(reg.all_of<PrevTransformComponent>(e));
	CHECK(reg.get<PrevTransformComponent>(e).PrevModel == frame0);

	// During frame 1 the transform moves; the snapshot has not run yet, so Prev still holds frame 0.
	reg.get<TransformComponent>(e).Position = {5.0f, 2.0f, 3.0f};
	const glm::mat4 frame1 = reg.get<TransformComponent>(e).GetTransformMatrix();
	REQUIRE(frame1 != frame0);
	CHECK(reg.get<PrevTransformComponent>(e).PrevModel == frame0);

	// End of frame 1: Prev catches up to the matrix frame 1 rendered with.
	system.Execute(Timestep{});
	CHECK(reg.get<PrevTransformComponent>(e).PrevModel == frame1);

	// A frame without movement keeps Prev == current (a stopped object reports zero motion).
	system.Execute(Timestep{});
	CHECK(reg.get<PrevTransformComponent>(e).PrevModel == frame1);
}

TEST_CASE("Snapshot skips entities without a mesh and snapshots every mesh", "[ecs][motion]")
{
	World world;
	auto& reg = world.GetRegistry();

	const auto bare = reg.create();
	reg.emplace<TransformComponent>(bare);

	const auto a = reg.create();
	reg.emplace<MeshComponent>(a);
	reg.emplace<TransformComponent>(a).Position = {1.0f, 0.0f, 0.0f};
	const auto b = reg.create();
	reg.emplace<MeshComponent>(b);
	reg.emplace<TransformComponent>(b).Position = {0.0f, 1.0f, 0.0f};

	PrevTransformSnapshotSystem system(&world);
	system.Execute(Timestep{});

	CHECK_FALSE(reg.all_of<PrevTransformComponent>(bare));
	CHECK(reg.get<PrevTransformComponent>(a).PrevModel == reg.get<TransformComponent>(a).GetTransformMatrix());
	CHECK(reg.get<PrevTransformComponent>(b).PrevModel == reg.get<TransformComponent>(b).GetTransformMatrix());
}

TEST_CASE("Snapshot makes a stopped camera report zero motion", "[ecs][motion]")
{
	World world;
	auto& reg = world.GetRegistry();
	const auto cam = reg.create();
	auto& rt = reg.emplace<CameraRuntimeComponent>(cam);
	rt.PrevViewProjection = glm::mat4(2.0f);
	rt.ViewProjection = glm::mat4(3.0f);

	PrevTransformSnapshotSystem system(&world);
	system.Execute(Timestep{});

	CHECK(reg.get<CameraRuntimeComponent>(cam).PrevViewProjection == glm::mat4(3.0f));
	CHECK(reg.get<CameraRuntimeComponent>(cam).ViewProjection == glm::mat4(3.0f));
}
