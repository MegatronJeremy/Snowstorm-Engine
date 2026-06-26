#pragma once

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/ECS/System.hpp"

#include <cstdint>

namespace Snowstorm
{
	class ViewportDisplaySystem final : public System
	{
	public:
		explicit ViewportDisplaySystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Current gizmo operation, cycled with W/E/R (matches ImGuizmo::OPERATION values:
		// TRANSLATE=7, ROTATE=120, SCALE=896). Stored as int to keep ImGuizmo out of the header.
		int m_GizmoOp = 7; // TRANSLATE

		// Gizmo-drag tracking for a single undo step per drag (not per frame). On the frame the drag
		// begins we capture the transform before any edit; on release we push one TransformCommand.
		bool m_GizmoDragging = false;
		TransformComponent m_DragBefore;
	};
}
