#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Panels/RenderSettingsPanel.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Service/EditorTheme.hpp"

namespace Snowstorm
{
	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin(EditorTheme::PanelTitle("Settings").c_str());

		// "Settings" holds the knobs you tune, "Performance" the read-only timings.
		if (ImGui::BeginTabBar("##settings_tabs"))
		{
			if (ImGui::BeginTabItem("Settings"))
			{
				DrawRenderSettings(ServiceView<RendererService>());
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Performance"))
			{
				m_PerformancePanel.OnImGuiRender(*m_World, ServiceView<RendererService>());
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
