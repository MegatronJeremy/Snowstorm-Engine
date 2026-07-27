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

		// RT picking (#118 follow-up): a GPU pick-ray trace reads back a frame or two after the click, so the
		// request and the resulting selection land on different frames. m_PickPending guards the poll;
		// m_PickWasDoubleClick carries the click's double-ness so the deferred result can still frame the camera.
		bool m_PickPending = false;
		bool m_PickWasDoubleClick = false;
	};
}
