#pragma once

#include "Snowstorm/Core/Base.hpp"
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

		void onImGuiRender();

	private:
		void drawEntityNode(Entity entity);

		static void drawComponents(Entity entity);

		World* m_World{};
		Entity m_SelectionContext;
	};
}
