#pragma once

#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/World/Entity.hpp"

namespace Snowstorm
{
	// Editor-wide "currently selected entity", shared by the hierarchy panel, the inspector, the
	// viewport gizmo, and mouse picking so they all agree on one selection. Lives on the World so
	// any system can read it via GetSingleton; it is editor-only and never serialized.
	class EditorSelectionSingleton final : public Singleton
	{
	public:
		Entity Selected;

		// True while the viewport gizmo is actively being dragged (ImGuizmo::IsUsing()). Set by
		// ViewportDisplaySystem each frame. Systems that also write the selected entity's transform
		// (e.g. RotatorSystem) skip it while this is set, so the animation doesn't fight the manual edit
		// -- the editor-authoring-wins convention (cf. Unreal not ticking an actor you're manipulating).
		bool GizmoActive = false;
	};
}
