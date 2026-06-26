#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <rttr/registration.h>

#include "SceneHierarchyPanel.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/World/EditorCommands.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include <nlohmann/json.hpp>

#include "Service/EditorTheme.hpp"

#include <cstring>

namespace Snowstorm
{
	SceneHierarchyPanel::SceneHierarchyPanel(World* context)
	{
		SetContext(context);
	}

	void SceneHierarchyPanel::SetContext(World* context)
	{
		m_World = context;
		SetSelected({});
	}

	Entity SceneHierarchyPanel::GetSelected() const
	{
		return m_World->GetSingleton<EditorSelectionSingleton>().Selected;
	}

	void SceneHierarchyPanel::SetSelected(const Entity entity) const
	{
		m_World->GetSingleton<EditorSelectionSingleton>().Selected = entity;
	}

	Entity SceneHierarchyPanel::DuplicateEntity(const Entity src) const
	{
		const std::string name = src.GetComponent<TagComponent>().Tag + " (Copy)";
		Entity dst = m_World->CreateEntity(name); // fresh IDComponent UUID + TagComponent

		// Copy every registered component except identity/name (those are set by CreateEntity).
		for (const auto& info : GetComponentRegistry())
		{
			if (!info.CopyFn || !info.Type.is_valid())
			{
				continue;
			}
			const std::string typeName = info.Type.get_name().to_string();
			if (typeName == "Snowstorm::IDComponent" || typeName == "Snowstorm::TagComponent")
			{
				continue;
			}
			info.CopyFn(src, dst);
		}
		return dst;
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");
		EditorTheme::SectionHeader("Scene Hierarchy");

		auto& cmds = m_World->GetSingleton<EditorCommandsSingleton>();

		if (ImGui::Button("+ Create Entity") && cmds.CreateEntity)
		{
			const Entity created = cmds.CreateEntity();
			SetSelected(created);
			if (created)
			{
				m_World->GetSingleton<EditorHistorySingleton>().Push(
				    CreateRef<AddEntityCommand>(created.GetComponent<IDComponent>().Id, "Create Entity"));
			}
		}
		ImGui::Separator();

		// Only entities that are part of the scene model
		auto view = m_World->GetRegistry().view<IDComponent, TagComponent>();

		for (const entt::entity e : view)
		{
			Entity entity{e, m_World};
			DrawEntityNode(entity);
		}

		// Clicking empty space clears selection (but not while a right-click context menu is up).
		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
		{
			SetSelected({});
		}

		ImGui::End();

		// ---- Apply deferred actions after the view iteration above ----
		auto& history = m_World->GetSingleton<EditorHistorySingleton>();

		if (m_PendingDuplicate)
		{
			const Entity dup = DuplicateEntity(m_PendingDuplicate);
			SetSelected(dup);
			if (dup)
			{
				history.Push(CreateRef<AddEntityCommand>(dup.GetComponent<IDComponent>().Id, "Duplicate Entity"));
			}
			m_PendingDuplicate = {};
		}
		if (m_PendingDelete)
		{
			if (GetSelected() == m_PendingDelete)
			{
				SetSelected({});
			}
			// Snapshot before destroying so the delete can be undone (restores same UUID/identity).
			nlohmann::json snapshot;
			const UUID uuid = m_PendingDelete.GetComponent<IDComponent>().Id;
			if (SceneSerializer::SerializeEntity(m_PendingDelete, snapshot) && cmds.DeleteEntity)
			{
				cmds.DeleteEntity(m_PendingDelete); // deferred destroy at end of frame
				history.Push(CreateRef<DeleteEntityCommand>(uuid, std::move(snapshot)));
			}
			m_PendingDelete = {};
		}

		// Rename modal (opened from the context menu).
		if (m_OpenRenamePopup)
		{
			ImGui::OpenPopup("Rename Entity");
			m_OpenRenamePopup = false;
		}
		if (ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::SetNextItemWidth(280.0f);
			const bool enter = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
			                                    ImGuiInputTextFlags_EnterReturnsTrue);
			if ((ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || enter) && m_RenameTarget)
			{
				const std::string before = m_RenameTarget.GetComponent<TagComponent>().Tag;
				const std::string after = m_RenameBuffer;
				if (after != before)
				{
					m_RenameTarget.WriteComponent<TagComponent>().Tag = after;
					m_World->GetSingleton<EditorHistorySingleton>().Push(
					    CreateRef<RenameCommand>(m_RenameTarget.GetComponent<IDComponent>().Id, before, after));
				}
				m_RenameTarget = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			{
				m_RenameTarget = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::Begin("Properties");
		EditorTheme::SectionHeader("Properties");
		if (const Entity selected = GetSelected())
		{
			DrawComponents(selected);
		}
		else
		{
			EditorTheme::WarningBanner("NO ENTITY SELECTED");
		}
		ImGui::End();

		// Apply any component removals requested by the inspector this frame (after DrawComponents).
		FlushComponentRemovals();
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		const auto& tag = entity.GetComponent<TagComponent>().Tag;

		ImGuiTreeNodeFlags flags = ((GetSelected() == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;

		const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity))),
		                                      flags,
		                                      "%s", tag.c_str());
		if (ImGui::IsItemClicked())
		{
			SetSelected(entity);
		}

		// Per-entity context menu: Rename / Duplicate / Delete (deferred to avoid view invalidation).
		if (ImGui::BeginPopupContextItem())
		{
			SetSelected(entity);
			if (ImGui::MenuItem("Rename"))
			{
				m_RenameTarget = entity;
				strncpy_s(m_RenameBuffer, tag.c_str(), sizeof(m_RenameBuffer) - 1);
				m_OpenRenamePopup = true;
			}
			if (ImGui::MenuItem("Duplicate"))
			{
				m_PendingDuplicate = entity;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete"))
			{
				m_PendingDelete = entity;
			}
			ImGui::EndPopup();
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

		ImGui::Spacing();
		if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0.0f)))
		{
			ImGui::OpenPopup("##addcomponent");
		}

		if (ImGui::BeginPopup("##addcomponent"))
		{
			bool anyAvailable = false;
			for (const auto& info : GetComponentRegistry())
			{
				// Offer only components the entity lacks and that are inspector-facing.
				if (!info.DrawInEditor || !info.HasFn || !info.EmplaceDefaultFn || !info.Type.is_valid())
				{
					continue;
				}
				if (info.HasFn(entity) || !IsComponentRemovable(info.Type))
				{
					continue; // already present, or structural (ID/Tag)
				}

				anyAvailable = true;
				const std::string label = PrettyComponentName(info.Type.get_name().to_string());
				if (ImGui::MenuItem(label.c_str()))
				{
					info.EmplaceDefaultFn(entity);
					ImGui::CloseCurrentPopup();
				}
			}

			if (!anyAvailable)
			{
				ImGui::TextDisabled("(no more components)");
			}
			ImGui::EndPopup();
		}
	}
}
