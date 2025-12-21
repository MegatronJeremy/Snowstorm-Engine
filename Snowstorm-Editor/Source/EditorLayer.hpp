#pragma once

#include <Snowstorm.h>
#include "Panels/SceneHierarchyPanel.hpp"

namespace Snowstorm
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() override = default;

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(Timestep ts) override;
		void OnEvent(Event& event) override;

	private:
		Ref<World> m_ActiveWorld;

		Entity m_RenderTargetEntity;
	};
}
