#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <rttr/registration.h>

#include "SceneHierarchyPanel.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	SceneHierarchyPanel::SceneHierarchyPanel(World* context)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(World* context)
	{
		m_World = context;
		m_SelectionContext = {};
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");

		// Only entities that are part of the scene model
		auto view = m_World->GetRegistry().view<IDComponent, TagComponent>();

		for (const entt::entity e : view)
		{
			Entity entity{ e, m_World };
			DrawEntityNode(entity);
		}

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		{
			m_SelectionContext = {};
		}

		ImGui::End();

		ImGui::Begin("Properties");
		if (m_SelectionContext)
		{
			DrawComponents(m_SelectionContext);
		}
		ImGui::End();
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		const auto& tag = entity.GetComponent<TagComponent>().Tag;

		ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0)
			| ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_Leaf;

		const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint32_t>(entity)),
		                                      flags,
		                                      "%s", tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_SelectionContext = entity;
		}

		if (opened)
		{
			ImGui::TreePop();
		}
	}

	void SceneHierarchyPanel::DrawComponents(const Entity entity)
	{
		for (const auto& info : GetComponentRegistry())
		{
			info.DrawFn(entity);
		}
	}
}
