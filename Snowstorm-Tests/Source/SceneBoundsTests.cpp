#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Math/CameraFraming.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;
using Catch::Approx;

// Mesh construction creates Vulkan buffers, so a resolved MeshInstance cannot exist here: the transformed
// and unioned AABB paths are not covered. What a GPU-free World can reach is the "nothing resolved" side
// of the bounds queries and everything after the AABB is known (framing, viewport camera resolution).

TEST_CASE("Renderable AABB queries report nothing without a resolved mesh", "[scenebounds]")
{
	World world;
	AABB box{glm::vec3(1.0f), glm::vec3(2.0f)};
	CHECK_FALSE(ComputeWorldRenderableAABB(world, box));
	CHECK(box.Min == glm::vec3(1.0f));
	CHECK(box.Max == glm::vec3(2.0f));

	Entity unresolved = world.CreateEntity("Unresolved");
	unresolved.AddComponent<TransformComponent>();
	unresolved.AddComponent<MeshComponent>();
	CHECK_FALSE(ComputeWorldRenderableAABB(world, box));
	CHECK_FALSE(ComputeEntityRenderableAABB(world, unresolved.Handle(), box));

	Entity noMesh = world.CreateEntity("NoMesh");
	noMesh.AddComponent<TransformComponent>();
	CHECK_FALSE(ComputeEntityRenderableAABB(world, noMesh.Handle(), box));
	CHECK(box.Min == glm::vec3(1.0f));
}

TEST_CASE("FrameCameraOnEntity refuses an entity with no resolved mesh and leaves the camera alone", "[scenebounds]")
{
	World world;
	Entity cam = world.CreateEntity("Editor Camera");
	cam.AddComponent<CameraComponent>();
	cam.AddComponent<DoNotSerializeComponent>();
	auto& tr = cam.AddComponent<TransformComponent>();
	tr.Position = glm::vec3(3.0f, 4.0f, 5.0f);

	Entity target = world.CreateEntity("Target");
	target.AddComponent<TransformComponent>();
	target.AddComponent<MeshComponent>();

	CHECK_FALSE(FrameCameraOnEntity(world, target.Handle()));
	CHECK(world.GetRegistry().Read<TransformComponent>(cam.Handle()).Position == glm::vec3(3.0f, 4.0f, 5.0f));
}

TEST_CASE("FramePrimaryCameraOnAABB moves the editor camera to the pure framing pose", "[scenebounds]")
{
	World world;
	Entity cam = world.CreateEntity("Editor Camera");
	auto& camComp = cam.AddComponent<CameraComponent>();
	camComp.Primary = false; // the editor camera is found by DoNotSerialize, not by Primary
	cam.AddComponent<DoNotSerializeComponent>();
	cam.AddComponent<TransformComponent>();
	cam.AddComponent<CameraControllerRuntimeComponent>();

	const AABB bounds{glm::vec3(-1.0f, 0.0f, -2.0f), glm::vec3(3.0f, 2.0f, 2.0f)};
	const FramingPose pose = ComputeFramingPose(bounds, camComp.PerspectiveFOV);
	const float oldNear = camComp.PerspectiveNear;
	const float oldFar = camComp.PerspectiveFar;

	FramePrimaryCameraOnAABB(world, bounds);

	auto& reg = world.GetRegistry();
	const auto& tr = reg.Read<TransformComponent>(cam.Handle());
	CHECK(tr.Position.x == Approx(pose.Position.x));
	CHECK(tr.Position.y == Approx(pose.Position.y));
	CHECK(tr.Position.z == Approx(pose.Position.z));
	CHECK(tr.Rotation.x == Approx(pose.Pitch));
	CHECK(tr.Rotation.y == Approx(pose.Yaw));
	CHECK(tr.Rotation.z == 0.0f);

	const auto& rt = reg.Read<CameraControllerRuntimeComponent>(cam.Handle());
	CHECK(rt.TargetPitch == Approx(pose.Pitch));
	CHECK(rt.TargetYaw == Approx(pose.Yaw));

	// Interactive focus leaves the frustum alone.
	CHECK(reg.Read<CameraComponent>(cam.Handle()).PerspectiveNear == oldNear);
	CHECK(reg.Read<CameraComponent>(cam.Handle()).PerspectiveFar == oldFar);

	// The box centre lies in front of the eye (the camera looks down -Z at yaw 0) inside the fitted clip range.
	const float depth = pose.Position.z - bounds.Center().z;
	CHECK(depth > pose.Near);
	CHECK(depth < pose.Far);
}

TEST_CASE("FramePrimaryCameraOnAABB fits the clip planes only on request", "[scenebounds]")
{
	World world;
	Entity cam = world.CreateEntity("Editor Camera");
	cam.AddComponent<CameraComponent>();
	cam.AddComponent<DoNotSerializeComponent>();
	cam.AddComponent<TransformComponent>();

	const AABB bounds{glm::vec3(-50.0f), glm::vec3(50.0f)};
	const FramingPose pose = ComputeFramingPose(bounds, world.GetRegistry().Read<CameraComponent>(cam.Handle()).PerspectiveFOV);

	FramePrimaryCameraOnAABB(world, bounds, true);

	const auto& c = world.GetRegistry().Read<CameraComponent>(cam.Handle());
	CHECK(c.PerspectiveNear == Approx(pose.Near));
	CHECK(c.PerspectiveFar == Approx(pose.Far));
	// Every corner of the box is within the far plane from the eye.
	const glm::vec3 eye = world.GetRegistry().Read<TransformComponent>(cam.Handle()).Position;
	CHECK(glm::length(bounds.Max - eye) < c.PerspectiveFar);
	CHECK(glm::length(bounds.Min - eye) < c.PerspectiveFar);
}

TEST_CASE("FramePrimaryCameraOnAABB ignores an authored gameplay camera", "[scenebounds]")
{
	World world;
	Entity game = world.CreateEntity("Game Camera");
	game.AddComponent<CameraComponent>().Primary = true;
	game.AddComponent<TransformComponent>().Position = glm::vec3(7.0f);

	FramePrimaryCameraOnAABB(world, AABB{glm::vec3(0.0f), glm::vec3(1.0f)});
	CHECK(world.GetRegistry().Read<TransformComponent>(game.Handle()).Position == glm::vec3(7.0f));
}

TEST_CASE("ResolveViewportCamera prefers a Primary camera targeting the viewport", "[scenebounds]")
{
	World world;
	auto& reg = world.GetRegistry();
	constexpr entt::entity none = entt::null;
	CHECK(ResolveViewportCamera(reg, none) == none);

	Entity viewport = world.CreateEntity("Viewport");
	Entity other = world.CreateEntity("Other Viewport");
	CHECK(ResolveViewportCamera(reg, viewport.Handle()) == none);

	Entity first = world.CreateEntity("First");
	first.AddComponent<CameraComponent>().Primary = false;
	first.AddComponent<CameraTargetComponent>().TargetViewportEntity = viewport.Handle();

	Entity elsewhere = world.CreateEntity("Elsewhere");
	elsewhere.AddComponent<CameraComponent>().Primary = true;
	elsewhere.AddComponent<CameraTargetComponent>().TargetViewportEntity = other.Handle();

	// No Primary on this viewport: the first candidate wins; a Primary on another viewport does not count.
	CHECK(ResolveViewportCamera(reg, viewport.Handle()) == first.Handle());

	Entity primary = world.CreateEntity("Primary");
	primary.AddComponent<CameraComponent>().Primary = true;
	primary.AddComponent<CameraTargetComponent>().TargetViewportEntity = viewport.Handle();
	CHECK(ResolveViewportCamera(reg, viewport.Handle()) == primary.Handle());
}
