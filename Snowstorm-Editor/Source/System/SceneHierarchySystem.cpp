#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Snowstorm/Render/RendererAPI.hpp"
#include "Service/EditorTheme.hpp"

namespace Snowstorm
{
	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Settings");
		EditorTheme::SectionHeader("System");

		// const auto stats = Renderer2D::GetStats();
		// ImGui::Text("Renderer2D Stats:");
		// ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		// ImGui::Text("Quads: %d", stats.QuadCount);
		// ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		// ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

		ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

		ImGui::End();
	}
}
