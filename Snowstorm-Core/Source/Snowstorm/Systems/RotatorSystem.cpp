#include "RotatorSystem.hpp"

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Snowstorm
{
	void RotatorSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		const float dt = ts.GetSeconds();

		// The entity currently being dragged by the editor gizmo (if any). While it's manipulated we skip
		// its rotation so the animation doesn't fight the manual edit (jitter through the Euler round-trip).
		// Editor-only singleton: guarded so the runtime (no editor) is unaffected. Editor-authoring-wins.
		Entity gizmoHeld;
		if (m_World->HasSingleton<EditorSelectionSingleton>())
		{
			if (const auto& sel = m_World->GetSingleton<EditorSelectionSingleton>(); sel.GizmoActive)
			{
				gizmoHeld = sel.Selected;
			}
		}

		for (const auto view = View<TransformComponent, RotatorComponent>(); const entt::entity e : view)
		{
			if (gizmoHeld && gizmoHeld.Handle() == e)
			{
				continue; // being manipulated by the gizmo this frame -> don't fight it
			}

			const auto& rot = reg.Read<RotatorComponent>(e);

			const float lenSq = glm::dot(rot.Axis, rot.Axis);
			if (lenSq < 1e-12f || rot.SpeedDegPerSec == 0.0f)
			{
				continue; // no axis or no speed -> nothing to do
			}

			const glm::vec3 axis = rot.Axis / glm::sqrt(lenSq);
			const float deltaAngle = glm::radians(rot.SpeedDegPerSec) * dt;

			auto& tr = reg.Write<TransformComponent>(e); // tracked write -> ChangedView fires

			// Build the current orientation with the SAME Y->X->Z order TransformComponent uses
			// (Ry * Rx * Rz), apply the incremental rotation about the (local) axis, then read the
			// Euler angles back in that order so TransformComponent.Rotation stays canonical and the
			// round-trip introduces no drift.
			const glm::mat4 currentMat = glm::eulerAngleYXZ(tr.Rotation.y, tr.Rotation.x, tr.Rotation.z);
			const glm::quat current = glm::quat_cast(currentMat);
			const glm::quat delta = glm::angleAxis(deltaAngle, axis);
			const glm::quat next = glm::normalize(current * delta);

			float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
			glm::extractEulerAngleYXZ(glm::mat4_cast(next), yaw, pitch, roll);

			tr.Rotation = glm::vec3(pitch, yaw, roll);
		}
	}
}
