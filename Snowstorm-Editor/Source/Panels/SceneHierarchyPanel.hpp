#pragma once

#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"

namespace Snowstorm
{
	class SceneHierarchyPanel
	{
	public:
		SceneHierarchyPanel() = default;
		explicit SceneHierarchyPanel(World* context);

		void SetContext(World* context);

		void OnImGuiRender();

	private:
		void DrawEntityNode(Entity entity);

		static void DrawComponents(Entity entity);

		// Selection now lives in EditorSelectionSingleton (shared with viewport/gizmo/picking).
		[[nodiscard]] Entity GetSelected() const;
		void SetSelected(Entity entity) const;

		Entity DuplicateEntity(Entity src) const;

		World* m_World{};

		// Deferred per-frame actions so we never mutate the ECS while iterating the hierarchy view.
		Entity m_PendingDelete;
		Entity m_PendingDuplicate;
		Entity m_RenameTarget;
		char m_RenameBuffer[256] = {};
		bool m_OpenRenamePopup = false;
	};
}
