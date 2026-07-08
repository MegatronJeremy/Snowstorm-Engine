#include "VisibilitySystem.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include "Snowstorm/Components/ViewportComponent.hpp"

#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/JobSystem.hpp"

#include <atomic>
#include <vector>

namespace Snowstorm
{
	bool VisibilitySystem::IsVisibilityDirtyThisFrame() const
	{
		// Any relevant component changed?
		if (!ChangedView<TransformComponent>().empty())
			return true;
		if (!ChangedView<CameraComponent>().empty())
			return true;
		if (!ChangedView<ViewportComponent>().empty())
			return true;
		if (!ChangedView<VisibilityComponent>().empty())
			return true;
		if (!ChangedView<CameraVisibilityComponent>().empty())
			return true;

		// Mesh/material resolution changes can change what is drawable (null -> valid, material swap, etc.)
		if (!ChangedView<MeshComponent>().empty())
			return true;
		if (!ChangedView<MaterialComponent>().empty())
			return true;

		// Any relevant component added/removed?
		if (!InitView<TransformComponent>().empty())
			return true;
		if (!InitView<CameraComponent>().empty())
			return true;
		if (!InitView<MeshComponent>().empty())
			return true;
		if (!InitView<MaterialComponent>().empty())
			return true;
		if (!InitView<VisibilityComponent>().empty())
			return true;
		if (!InitView<CameraVisibilityComponent>().empty())
			return true;
		if (!InitView<ViewportComponent>().empty())
			return true;
		if (!InitView<CameraTargetComponent>().empty())
			return true;

		if (!FiniView<TransformComponent>().empty())
			return true;
		if (!FiniView<CameraComponent>().empty())
			return true;
		if (!FiniView<MeshComponent>().empty())
			return true;
		if (!FiniView<MaterialComponent>().empty())
			return true;
		if (!FiniView<VisibilityComponent>().empty())
			return true;
		if (!FiniView<CameraVisibilityComponent>().empty())
			return true;
		if (!FiniView<ViewportComponent>().empty())
			return true;
		if (!FiniView<CameraTargetComponent>().empty())
			return true;

		return false;
	}

	void VisibilitySystem::Execute(Timestep)
	{
		// ---- Dirty early out ----
		if (!IsVisibilityDirtyThisFrame())
		{
			return;
		}

		auto& reg = m_World->GetRegistry();

		// Cameras that can produce visibility
		const auto camView = reg.view<TransformComponent, CameraRuntimeComponent, CameraTargetComponent, CameraVisibilityComponent>();

		// Renderables we cull (meshes). Snapshot the candidate set ONCE into a contiguous array: it's the
		// same for every camera (only the frustum/mask differ), and ParallelGather needs an index-able
		// range to slice across workers (an EnTT multi-component view isn't random-access).
		const auto meshView = reg.view<TransformComponent, MeshComponent, MaterialComponent, VisibilityComponent>();
		const std::vector<entt::entity> candidates(meshView.begin(), meshView.end());

		auto& jobs = Application::Get().GetServiceManager().GetService<JobSystem>();
		// ecs.parallel off (or CVar) -> grain >= count forces ParallelGather's inline serial path, so the
		// on/off toggle is a pure perf switch with identical (deterministic, ordered) output.
		const size_t grain = CVars::EcsParallel.Get() ? size_t{256} : candidates.size() + 1;

		for (const entt::entity camE : camView)
		{
			const auto& camRT = reg.Read<CameraRuntimeComponent>(camE);
			const auto& camTarget = reg.Read<CameraTargetComponent>(camE);
			const auto& camVis = reg.Read<CameraVisibilityComponent>(camE);

			// Must have a resolved viewport target (runtime cache)
			if (camTarget.TargetViewportEntity == entt::null || !reg.valid(camTarget.TargetViewportEntity))
			{
				continue;
			}

			const VisibilityMask camMask = camVis.Mask;

			// Diagnostic counter: renderables eligible for this camera (resolved + layer-matched) before the
			// frustum test. Incremented from worker threads, so it's atomic — it's touched at most once per
			// candidate and never read in the hot path, so the single-counter contention is negligible next
			// to the frustum math (and it's order-independent, unlike the gathered list).
			std::atomic<uint32_t> considered{0};

			// Parallel frustum cull: each candidate is tested independently (reads only, no shared writes)
			// and emits its own entity iff visible. ParallelGather returns the passers in deterministic
			// index order, so the visible set + draw order match the old serial loop bit-for-bit.
			std::vector<entt::entity> visible = jobs.ParallelGather<entt::entity>(
			    candidates.size(),
			    [&](const size_t i, auto&& emit)
			    {
				    const entt::entity e = candidates[i];
				    const auto& mesh = reg.Read<MeshComponent>(e);
				    const auto& mat = reg.Read<MaterialComponent>(e);
				    const auto& vis = reg.Read<VisibilityComponent>(e);

				    // Must be resolved
				    if (!mesh.MeshInstance || !mat.MaterialInstance)
				    {
					    return;
				    }

				    // Layer filtering
				    if ((vis.Mask & camMask) == 0)
				    {
					    return;
				    }

				    // Eligible for this camera (resolved + layer-matched); frustum culling decides the rest.
				    considered.fetch_add(1, std::memory_order_relaxed);

				    // Frustum culling
				    const auto& tr = reg.Read<TransformComponent>(e);
				    const glm::mat4 M = tr.GetTransformMatrix();

				    const MeshBounds& localB = mesh.MeshInstance->GetBounds();

				    // Sphere first (cheap)
				    const Sphere ws = TransformSphere(localB.Sphere, M);
				    if (!camRT.frustum.IntersectsSphere(ws.Center, ws.Radius))
				    {
					    return;
				    }

				    // AABB second (tighter)
				    const AABB wa = TransformAABB(localB.Box, M);
				    if (!camRT.frustum.IntersectsAABB(wa))
				    {
					    return;
				    }

				    emit(e);
			    },
			    grain);

			// Publish this frame's result on the main thread (tracked write; runtime-only cache).
			auto& cache = reg.emplace_or_replace<VisibilityCacheComponent>(camE);
			cache.VisibleMeshes = std::move(visible);
			cache.Considered = considered.load(std::memory_order_relaxed);
		}
	}
}
