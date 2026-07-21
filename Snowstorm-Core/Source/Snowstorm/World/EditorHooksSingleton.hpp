#pragma once

#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <entt/entt.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace Snowstorm
{
	// The editor-integration seam for Core (the inversion in #162). A handful of Core-run systems need to
	// REACT to editor concepts — a gizmo drag, an inspector edit, a scene clear — but Core must not depend
	// on the editor's concrete types (undo history, selection). So Core defines this callback holder and
	// invokes it; the editor INSTALLS the callbacks (see EditorLayer::RegisterEditorSystems), wiring them to
	// its real EditorSelection/EditorHistory singletons. A packaged runtime never registers this singleton,
	// or leaves the callbacks null — every call site null-checks, so Core is a clean no-op without the editor.
	//
	// This mirrors the WITH_EDITOR / editor-callback split real engines use (Unreal's editor delegates,
	// Unity's separate Editor assembly), in this codebase's existing idiom: EditorCommandsSingleton is the
	// same pattern (Core-owned struct of std::functions the editor fills). Prefer adding a hook here over
	// making Core name a new editor type.
	class EditorHooksSingleton final : public Singleton
	{
	public:
		// Seam 2 (RotatorSystem / any sim system): the entity currently being manipulated by the editor
		// gizmo, or entt::null if none. Simulation systems that also write that entity's transform skip it
		// so the animation doesn't fight the manual drag (editor-authoring-wins). Null callback -> null entity.
		std::function<entt::entity()> ManipulatedEntity;

		// Seam 3 (ComponentRegistry inspector undo coalescing): a continuous inspector edit (dragging a
		// slider) should collapse into ONE undo step. The reflection draw path calls BeginComponentEdit on
		// the first changed frame (snapshotting the pre-edit component JSON) and FinalizeComponentEdit once
		// the widget is released (post-edit JSON); HasPendingComponentEdit guards re-capture. All no-op when
		// unset (headless / runtime), so reflected edits still apply — they just aren't undoable.
		std::function<void(UUID /*target*/, const std::string& /*typeName*/, nlohmann::json /*before*/)> BeginComponentEdit;
		std::function<void(nlohmann::json /*after*/)> FinalizeComponentEdit;
		std::function<bool()> HasPendingComponentEdit;

		// Called by World::ClearSceneEntities after a scene wipe (Open/New Scene). The editor uses it to
		// drop selection/undo state that referenced the now-destroyed entities. Null -> nothing to reset.
		std::function<void()> OnSceneCleared;
	};
}
