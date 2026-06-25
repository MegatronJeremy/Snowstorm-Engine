#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Snowstorm/Render/RendererAPI.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Service/EditorTheme.hpp"

namespace Snowstorm
{
	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Settings");
		EditorTheme::SectionHeader("Performance");

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("FPS:        %.1f", io.Framerate);
		ImGui::Text("Frame:      %.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

		ImGui::Spacing();

		// Last scene-pass GPU submission stats (RendererSingleton::RenderStats). DrawCalls == Instances
		// today because there is no hardware instancing yet; when Batches is far below DrawCalls it
		// means many objects share (mesh, material) and could collapse into instanced draws.
		const RenderStats& stats = SingletonView<RendererSingleton>().GetStats();
		ImGui::Text("Draw calls: %u", stats.DrawCalls);
		ImGui::Text("Batches:    %u", stats.Batches);
		ImGui::Text("Instances:  %u", stats.Instances);
		ImGui::Text("Triangles:  %u", stats.Triangles);

		ImGui::End();
	}
}
