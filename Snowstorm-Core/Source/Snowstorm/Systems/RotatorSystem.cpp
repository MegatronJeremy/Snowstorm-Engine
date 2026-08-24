#include "RotatorSystem.hpp"

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/World/EditorHooksSingleton.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void RotatorSystem::Execute(const Timestep ts)
	{
		// Object motion has to obey the same clock as the camera route, or a capture puts the camera where
		// the harness expects and the props somewhere else, and the per-frame path-traced reference stops
		// matching the frame it is supposed to be ground truth for.
		//
		// AdvanceRotation accumulates into the transform rather than being a closed form of a frame index,
		// so the count of advances is what must be reproducible: step once per RENDERED frame, not once per
		// loop iteration. Logic runs unconditionally while the renderer's counter only moves after a
		// successful BeginFrame, so a skipped frame would otherwise rotate the props one step further than
		// the frame they are captured on, permanently.
		const uint64_t renderFrame = ServiceView<RendererService>().GetFrameCounter();
		const bool fixedStep = CVars::FixedSimulationStep();
		if (fixedStep && renderFrame == m_LastFrame)
		{
			return;
		}
		m_LastFrame = renderFrame;

		const float dt = CVars::SimulationStepSeconds(ts.GetSeconds());

		// The entity currently being dragged by the editor gizmo (if any). While it's manipulated we skip
		// its rotation so the animation doesn't fight the manual edit (jitter through the Euler round-trip).
		// Asked through the editor-integration hook so Core doesn't name the editor's selection type (#162);
		// unset callback (runtime) -> entt::null -> nothing skipped. Editor-authoring-wins. Read ONCE up
		// front (not per-entity) so the parallel workers only compare a captured handle.
		entt::entity gizmoHeld = entt::null;
		if (const auto& hooks = m_World->GetSingleton<EditorHooksSingleton>(); hooks.ManipulatedEntity)
		{
			gizmoHeld = hooks.ManipulatedEntity();
		}

		// Data-parallel: each rotator's update is pure per-entity math (no shared state), so it splits
		// cleanly across workers. Access is declared (Write<Transform> mutated in place, Read<Rotator>
		// input only), so ParallelForEach hands in the right constness and, after the barrier, marks
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
