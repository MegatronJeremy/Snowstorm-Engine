#include "RotatorSystem.hpp"

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void RotatorSystem::Execute(const Timestep ts)
	{
		const float dt = ts.GetSeconds();

		// The entity currently being dragged by the editor gizmo (if any). While it's manipulated we skip
		// its rotation so the animation doesn't fight the manual edit (jitter through the Euler round-trip).
		// Editor-only singleton: guarded so the runtime (no editor) is unaffected. Editor-authoring-wins.
		// Read ONCE up front (not per-entity) so the parallel workers only compare a captured handle.
		entt::entity gizmoHeld = entt::null;
		if (m_World->HasSingleton<EditorSelectionSingleton>())
		{
			if (const auto& sel = m_World->GetSingleton<EditorSelectionSingleton>(); sel.GizmoActive && sel.Selected)
			{
				gizmoHeld = sel.Selected.Handle();
			}
		}

		// Data-parallel: each rotator's update is pure per-entity math (no shared state), so it splits
		// cleanly across workers. Access is declared — Write<Transform> (mutated in place), Read<Rotator>
		// (input only) — so ParallelForEach hands in the right constness and, after the barrier, marks
		// Transform changed for ChangedView consumers (culling/camera-runtime). Serial fallback +
		// parallelism are handled inside, gated on the ecs.parallel CVar.
		ParallelForEach<Write<TransformComponent>, Read<RotatorComponent>>(
		    [dt, gizmoHeld](const entt::entity e, TransformComponent& tr, const RotatorComponent& rot)
		    {
			    if (gizmoHeld != entt::null && e == gizmoHeld)
			    {
				    return; // being manipulated by the gizmo this frame -> don't fight it
			    }
			    AdvanceRotation(tr, rot, dt); // shared pure math (see RotatorMath.hpp)
		    });
	}
}
