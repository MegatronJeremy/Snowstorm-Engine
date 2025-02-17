#pragma once

#include "Panels/SceneHieararchyPanel.hpp"

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class SceneHierarchySystem final : public System
	{
	public:
		explicit SceneHierarchySystem(const WorldRef& world)
			: System(world)
		{
			m_SceneHierarchyPanel.SetContext(world);
		}

		void Execute(Timestep ts) override;

	private:
		SceneHierarchyPanel m_SceneHierarchyPanel;
	};
}
