#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Frustum.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

using namespace Snowstorm;

// VisibilitySystem::Execute needs an Application (window, Vulkan) for its JobSystem and a Mesh (GPU buffers)
// for the local bounds, so the system itself cannot run here. These tests replay its cull body on the same
// primitives (TransformSphere/TransformAABB, Frustum, JobSystem::ParallelGather, the grain rule) over a
// World, which pins the serial-vs-parallel contract; wiring drift between the system and this copy is not
// caught.

namespace
{
	constexpr size_t kParallelGrain = 256;
	const MeshBounds kUnitCube{Sphere{glm::vec3(0.0f), 0.866025f}, AABB{glm::vec3(-0.5f), glm::vec3(0.5f)}};

	Frustum CameraAtOriginLookingDownNegZ()
	{
		const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 50.0f);
		return Frustum::FromViewProjection(proj);
	}

	Entity AddRenderable(World& world, const glm::vec3& position, const VisibilityMask mask = Visibility::Scene)
	{
		Entity e = world.CreateEntity("Renderable");
		e.AddComponent<TransformComponent>().Position = position;
		e.AddComponent<VisibilityComponent>().Mask = mask;
		return e;
	}

	std::vector<entt::entity> Cull(World& world, JobSystem& jobs, const Frustum& frustum, const VisibilityMask camMask,
	                               const bool parallel)
	{
		auto& reg = world.GetRegistry();
		const auto view = reg.view<TransformComponent, VisibilityComponent>();
		const std::vector<entt::entity> candidates(view.begin(), view.end());
		const size_t grain = parallel ? kParallelGrain : candidates.size() + 1;
		return jobs.ParallelGather<entt::entity>(
		    candidates.size(),
		    [&](const size_t i, auto&& emit)
		    {
			    const entt::entity e = candidates[i];
			    const glm::mat4 M = reg.Read<TransformComponent>(e).GetTransformMatrix();
			    if ((reg.Read<VisibilityComponent>(e).Mask & camMask) == 0)
			    {
				    return;
			    }
			    const Sphere s = TransformSphere(kUnitCube.Sphere, M);
			    if (!frustum.IntersectsSphere(s.Center, s.Radius))
			    {
				    return;
			    }
			    if (!frustum.IntersectsAABB(TransformAABB(kUnitCube.Box, M)))
			    {
				    return;
			    }
			    emit(e);
		    },
		    grain);
	}

	bool Contains(const std::vector<entt::entity>& v, const Entity& e)
	{
		return std::find(v.begin(), v.end(), e.Handle()) != v.end();
	}
}

TEST_CASE("Frustum cull keeps inside and straddling entities and drops outside ones", "[visibility]")
{
	World world;
	JobSystem jobs;
	const Frustum f = CameraAtOriginLookingDownNegZ();

	const Entity inside = AddRenderable(world, {0.0f, 0.0f, -10.0f});
	const Entity behind = AddRenderable(world, {0.0f, 0.0f, 10.0f});
	const Entity beyondFar = AddRenderable(world, {0.0f, 0.0f, -80.0f});
	const Entity farLeft = AddRenderable(world, {-100.0f, 0.0f, -10.0f});
	const Entity straddlesNear = AddRenderable(world, {0.0f, 0.0f, -0.5f});
	const Entity straddlesFar = AddRenderable(world, {0.0f, 0.0f, -50.0f});

	for (const bool parallel : {false, true})
	{
		const auto visible = Cull(world, jobs, f, Visibility::All, parallel);
		CHECK(Contains(visible, inside));
		CHECK(Contains(visible, straddlesNear));
		CHECK(Contains(visible, straddlesFar));
		CHECK_FALSE(Contains(visible, behind));
		CHECK_FALSE(Contains(visible, beyondFar));
		CHECK_FALSE(Contains(visible, farLeft));
		CHECK(visible.size() == 3);
	}
}

TEST_CASE("Frustum cull honours the camera visibility mask before the frustum", "[visibility]")
{
	World world;
	JobSystem jobs;
	const Frustum f = CameraAtOriginLookingDownNegZ();

	const Entity scene = AddRenderable(world, {0.0f, 0.0f, -10.0f}, Visibility::Scene);
	const Entity game = AddRenderable(world, {1.0f, 0.0f, -10.0f}, Visibility::Game);
	const Entity both = AddRenderable(world, {2.0f, 0.0f, -10.0f}, Visibility::Scene | Visibility::Game);

	const auto sceneCam = Cull(world, jobs, f, Visibility::Scene, false);
	CHECK(Contains(sceneCam, scene));
	CHECK(Contains(sceneCam, both));
	CHECK_FALSE(Contains(sceneCam, game));

	CHECK(Cull(world, jobs, f, Visibility::None, false).empty());
}

TEST_CASE("Frustum cull output is identical, and in candidate order, serial vs parallel", "[visibility]")
{
	World world;
	JobSystem jobs;
	const Frustum f = CameraAtOriginLookingDownNegZ();

	// 3000 entities on a lattice spanning well past the frustum on every side, so the parallel path splits
	// into many slices and each slice mixes visible and culled entities.
	constexpr int kSide = 15;
	for (int x = 0; x < kSide; ++x)
	{
		for (int y = 0; y < kSide; ++y)
		{
			for (int z = 0; z < 13; ++z)
			{
				const VisibilityMask mask = ((x + y + z) % 5 == 0) ? Visibility::Game : Visibility::Scene;
				AddRenderable(world, {(x - 7) * 6.0f, (y - 7) * 4.0f, 8.0f - z * 7.0f}, mask);
			}
		}
	}

	const auto serial = Cull(world, jobs, f, Visibility::Scene, false);
	REQUIRE_FALSE(serial.empty());
	REQUIRE(serial.size() < world.GetRegistry().view<TransformComponent>().size());

	for (int run = 0; run < 5; ++run)
	{
		CHECK(Cull(world, jobs, f, Visibility::Scene, true) == serial);
	}

	auto& reg = world.GetRegistry();
	const auto view = reg.view<TransformComponent, VisibilityComponent>();
	const std::vector<entt::entity> candidates(view.begin(), view.end());
	size_t cursor = 0;
	for (const entt::entity e : serial)
	{
		const auto it = std::find(candidates.begin() + static_cast<std::ptrdiff_t>(cursor), candidates.end(), e);
		REQUIRE(it != candidates.end()); // visible list is a subsequence of the candidates
		cursor = static_cast<size_t>(it - candidates.begin()) + 1;
	}
}
