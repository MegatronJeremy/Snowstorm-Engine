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

namespace Snowstorm
{
	bool VisibilitySystem::IsVisibilityDirtyThisFrame() const
	{
		// Any relevant component changed?
		if (!ChangedView<TransformComponent>().empty()) return true;
		if (!ChangedView<CameraComponent>().empty()) return true;
		if (!ChangedView<ViewportComponent>().empty()) return true;
		if (!ChangedView<VisibilityComponent>().empty()) return true;
		if (!ChangedView<CameraVisibilityComponent>().empty()) return true;

		// Mesh/material resolution changes can change what is drawable (null -> valid, material swap, etc.)
		if (!ChangedView<MeshComponent>().empty()) return true;
		if (!ChangedView<MaterialComponent>().empty()) return true;

		// Any relevant component added/removed?
		if (!InitView<TransformComponent>().empty()) return true;
		if (!InitView<CameraComponent>().empty()) return true;
		if (!InitView<MeshComponent>().empty()) return true;
		if (!InitView<MaterialComponent>().empty()) return true;
		if (!InitView<VisibilityComponent>().empty()) return true;
		if (!InitView<CameraVisibilityComponent>().empty()) return true;
		if (!InitView<ViewportComponent>().empty()) return true;
		if (!InitView<CameraTargetComponent>().empty()) return true;

		if (!FiniView<TransformComponent>().empty()) return true;
		if (!FiniView<CameraComponent>().empty()) return true;
		if (!FiniView<MeshComponent>().empty()) return true;
		if (!FiniView<MaterialComponent>().empty()) return true;
		if (!FiniView<VisibilityComponent>().empty()) return true;
		if (!FiniView<CameraVisibilityComponent>().empty()) return true;
		if (!FiniView<ViewportComponent>().empty()) return true;
		if (!FiniView<CameraTargetComponent>().empty()) return true;

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

		// Renderables we cull (meshes)
		const auto meshView = reg.view<TransformComponent, MeshComponent, MaterialComponent, VisibilityComponent>();

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

			// Ensure cache exists; clear it. (This is fine: it's a runtime list for THIS frame)
			auto& cache = reg.emplace_or_replace<VisibilityCacheComponent>(camE);
			cache.VisibleMeshes.clear();
			cache.VisibleMeshes.reserve(256);

			const VisibilityMask camMask = camVis.Mask;

			for (const entt::entity e : meshView)
			{
				const auto& mesh = reg.Read<MeshComponent>(e);
				const auto& mat  = reg.Read<MaterialComponent>(e);
				const auto& vis  = reg.Read<VisibilityComponent>(e);

				// Must be resolved
				if (!mesh.MeshInstance || !mat.MaterialInstance)
				{
					continue;
				}

				// Layer filtering
				if ((vis.Mask & camMask) == 0)
				{
					continue;
				}

				// Frustum culling
				const auto& tr = reg.Read<TransformComponent>(e);
				const glm::mat4 M = tr.GetTransformMatrix();

				const MeshBounds& localB = mesh.MeshInstance->GetBounds();

				// Sphere first (cheap)
				const Sphere ws = TransformSphere(localB.Sphere, M);
				if (!camRT.frustum.IntersectsSphere(ws.Center, ws.Radius))
				{
					continue;
				}

				// AABB second (tighter)
				const AABB wa = TransformAABB(localB.Box, M);
				if (!camRT.frustum.IntersectsAABB(wa))
				{
					continue;
				}

				cache.VisibleMeshes.push_back(e);
			}
		}
	}
}
