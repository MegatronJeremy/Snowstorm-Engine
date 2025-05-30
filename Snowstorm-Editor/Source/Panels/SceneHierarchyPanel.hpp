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

		void DrawComponents(Entity entity) const;

		World* m_World{};
		Entity m_SelectionContext;
	};
}
