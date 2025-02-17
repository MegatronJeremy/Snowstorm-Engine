#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Snowstorm/Render/Renderer2D.hpp"
#include "Snowstorm/World/Components.hpp"

namespace Snowstorm
{
	void SceneHierarchySystem::Execute(Timestep ts)
	{
		const auto entityView = View<TagComponent>(); // Just a view of all entities -> get all of their components

		m_SceneHierarchyPanel.onImGuiRender();

		ImGui::Begin("Settings");

		const auto stats = Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);
		ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

		// TODO be able to edit everything through RTTR
		// if (m_SquareEntity)
		// {
		// 	ImGui::Separator();
		// 	ImGui::Text("%s", m_SquareEntity.GetComponent<TagComponent>().Tag.c_str());
		//
		// 	auto& squareColor = m_SquareEntity.GetComponent<SpriteComponent>().TintColor;
		// 	ImGui::ColorEdit4("Square Color", value_ptr(squareColor));
		// 	ImGui::Separator();
		// }
		//
		// ImGui::DragFloat3("Camera Position", value_ptr(
		// 	                  m_CameraEntity.GetComponent<TransformComponent>().Position));
		//
		// if (ImGui::Checkbox("Camera A", &m_PrimaryCamera))
		// {
		// 	m_CameraEntity.GetComponent<CameraComponent>().Primary = m_PrimaryCamera;
		// 	m_SecondCamera.GetComponent<CameraComponent>().Primary = !m_PrimaryCamera;
		// }
		//
		// {
		// 	auto& camera = m_SecondCamera.GetComponent<CameraComponent>().Camera;
		// 	float orthoSize = camera.GetOrthographicSize();
		// 	if (ImGui::DragFloat("Second Camera Ortho Size", &orthoSize))
		// 		camera.SetOrthographicSize(orthoSize);
		// }

		ImGui::End();
	}
}
