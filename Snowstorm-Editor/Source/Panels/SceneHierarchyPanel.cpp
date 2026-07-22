#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <rttr/registration.h>

#include <cmath>

#include "SceneHierarchyPanel.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Singletons/EditorCommands.hpp"
#include "Singletons/EditorCommandsSingleton.hpp"
#include "Singletons/EditorHistorySingleton.hpp"
#include "Singletons/EditorSelectionSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "MaterialInspectorPanel.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include <nlohmann/json.hpp>

#include "Service/EditorTheme.hpp"

#include <cstring>

namespace Snowstorm
{
	namespace
	{
		// Structural entities must not be deleted from the editor: the viewport/render-target and the
		// cameras are required infrastructure (deleting the main viewport left RenderSystem dereferencing
		// a destroyed target → crash). Everything else is ordinary, deletable scene content.
		bool IsDeletable(const Entity entity)
		{
			return !entity.HasComponent<ViewportComponent>() &&
			       !entity.HasComponent<RenderTargetComponent>() &&
			       !entity.HasComponent<CameraComponent>();
		}

		// Stable asset handles for the "3D Object" presets (from Assets/AssetRegistry.json). Handles are the
		// engine's stable references; if these meshes/material are ever removed the preset just spawns an
		// unresolved (invisible) mesh rather than crashing.
		constexpr uint64_t kCubeMeshHandle = 5810267832183663728ull;
		constexpr uint64_t kQuadMeshHandle = 12112538743247314239ull;
		constexpr uint64_t kWhiteMaterialHandle = 14863079243352112687ull;

		// A new entity spawns a few units IN FRONT OF the editor camera, facing where the camera looks
		// (Unreal Place Actors / Unity scene-view spawn) — so it lands in view instead of at the origin
		// (which, in a large scene like Sponza, means hunting for it). Falls back to the origin + a downward
		// facing if no camera is found. Returns the spawn position and a forward direction for the preset.
		void EditorSpawnPose(World& world, glm::vec3& outPos, glm::vec3& outForward)
		{
			outPos = glm::vec3(0.0f);
			outForward = glm::vec3(0.0f, -1.0f, 0.0f); // default: aim down

			// Prefer the primary camera's runtime transform (the editor fly-camera).
			auto& reg = world.GetRegistry();
			for (auto view = reg.view<const CameraComponent, const CameraRuntimeComponent, const TransformComponent>();
			     const entt::entity e : view)
			{
				const auto& cam = reg.Read<CameraComponent>(e);
				if (!cam.Primary)
				{
					continue;
				}
				const auto& rt = reg.Read<CameraRuntimeComponent>(e);
				const glm::vec3 camPos = reg.Read<TransformComponent>(e).Position;
				// Camera forward = -Z of the view matrix's inverse == the third row of the view basis negated.
				// The view matrix rows are the camera basis; row 2 is the camera's +Z (backward), so forward
				// is its negation.
				const glm::vec3 forward = -glm::normalize(glm::vec3(rt.View[0][2], rt.View[1][2], rt.View[2][2]));
				outForward = forward;
				outPos = camPos + forward * 5.0f;
				return;
			}
		}

		// Convert a forward direction to a TransformComponent Euler rotation (X=pitch, Y=yaw, Z=roll) that
		// makes the entity's -Z axis point along `forward` — matches how spot/camera forward is derived
		// (ForwardFromPitchYaw in CameraControllerSystem). Roll is left 0.
		glm::vec3 EulerFromForward(const glm::vec3& forward)
		{
			const glm::vec3 f = glm::normalize(forward);
			const float yaw = std::atan2(-f.x, -f.z); // -Z forward convention
			const float pitch = std::asin(glm::clamp(f.y, -1.0f, 1.0f));
			return {pitch, yaw, 0.0f};
		}

		// The kind of glyph to draw beside a Create-menu row. These mirror the viewport light gizmo icons
		// (ViewportDisplaySystem) so the menu and the scene read the same visual language.
		enum class CreateIcon
		{
			Empty,
			Directional,
			Point,
			Spot,
			Cube
		};

		// Draw a small draw-list glyph for a Create-menu row and advance the ImGui cursor past it, so a
		// text label can sit to its right. Tinted `color`. Cheap vector art (no texture) matching the
		// gizmo glyphs: sun rays / bulb dot / spot triangle / cube wireframe / empty ring.
		void DrawCreateIcon(CreateIcon kind, ImU32 color)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const float h = ImGui::GetTextLineHeight();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const ImVec2 c{p.x + h * 0.5f, p.y + h * 0.5f};
			const float r = h * 0.32f;

			switch (kind)
			{
			case CreateIcon::Empty:
				dl->AddCircle(c, r, color, 12, 1.5f);
				break;
			case CreateIcon::Point:
				dl->AddCircleFilled(c, r * 0.55f, color, 12);
				dl->AddCircle(c, r, color, 12, 1.0f);
				break;
			case CreateIcon::Spot:
				dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.8f), ImVec2(c.x + r, c.y - r * 0.8f),
				                      ImVec2(c.x, c.y + r), color);
				break;
			case CreateIcon::Directional:
				dl->AddCircleFilled(c, r * 0.5f, color, 12);
				for (int k = 0; k < 8; ++k)
				{
					const float a = static_cast<float>(k) * 0.785398f;
					const ImVec2 d{std::cos(a), std::sin(a)};
					dl->AddLine(ImVec2(c.x + d.x * r * 0.8f, c.y + d.y * r * 0.8f),
					            ImVec2(c.x + d.x * r * 1.4f, c.y + d.y * r * 1.4f), color, 1.0f);
				}
				break;
			case CreateIcon::Cube:
				dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), color, 0.0f, 0, 1.5f);
				dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x - r * 0.4f, c.y - r * 1.5f), color, 1.0f);
				dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r * 1.6f, c.y - r * 1.5f), color, 1.0f);
				dl->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 1.5f), ImVec2(c.x + r * 1.6f, c.y - r * 1.5f), color, 1.0f);
				break;
			}
			ImGui::Dummy(ImVec2(h, h)); // reserve the icon cell so the label lays out to its right
			ImGui::SameLine();
		}
	}

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
		// Route through SelectEntity so picking an entity clears any Content-Browser asset selection
		// (the Properties panel shows one xor the other).
		m_World->GetSingleton<EditorSelectionSingleton>().SelectEntity(entity);
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

		// Delete key removes the selected entity (same deferred path as the right-click Delete). Gated
		// on the keyboard not being captured (e.g. a rename field) so typing never deletes.
		if (const auto& input = m_World->GetSingleton<InputStateSingleton>();
		    input.PressedThisFrame.test(Key::Delete) && !input.WantTextInput)
		{
			if (const Entity sel = GetSelected(); sel && IsDeletable(sel))
			{
				m_PendingDelete = sel;
			}
		}

		// "+ Create" menu: preset entities (Unity GameObject menu / Unreal Place Actors). Each preset
		// spawns a fully-formed entity in front of the editor camera, renames it, selects it, and records
		// one undo step -- instead of the old bare-entity button that left you hand-assembling components.
		// The configurator lambda adds the preset's components to the freshly created entity.
		const auto createPreset = [&](const char* presetName, const auto& configure)
		{
			if (!cmds.CreateEntity)
			{
				return;
			}
			Entity created = cmds.CreateEntity();
			if (!created)
			{
				return;
			}
			created.GetComponentMutable_Untracked<TagComponent>().Tag = presetName;

			glm::vec3 pos, forward;
			EditorSpawnPose(*m_World, pos, forward);
			auto& tr = created.AddComponent<TransformComponent>();
			tr.Position = pos;

			configure(created, forward);

			SetSelected(created);
			m_World->GetSingleton<EditorHistorySingleton>().Push(
			    CreateRef<AddEntityCommand>(created.GetComponent<IDComponent>().Id, "Create Entity"));
		};

		// Preset configurators, reused by both the "+ CREATE" popup and the right-click context menu.
		const auto addDirectional = [](Entity e, const glm::vec3& fwd)
		{
			auto& dl = e.AddComponent<DirectionalLightComponent>();
			dl.Direction = fwd;
			dl.Color = glm::vec3(1.0f, 0.98f, 0.95f);
			dl.Intensity = 1.0f;
			e.AddComponent<VisibilityComponent>();
		};
		const auto addPoint = [](Entity e, const glm::vec3&)
		{
			auto& pl = e.AddComponent<PointLightComponent>();
			pl.Color = glm::vec3(1.0f);
			pl.Intensity = 50.0f;
			pl.Range = 10.0f;
			e.AddComponent<VisibilityComponent>();
		};
		const auto addSpot = [](Entity e, const glm::vec3& fwd)
		{
			e.GetComponentMutable_Untracked<TransformComponent>().Rotation = EulerFromForward(fwd);
			auto& sl = e.AddComponent<SpotLightComponent>();
			sl.Color = glm::vec3(1.0f);
			sl.Intensity = 80.0f;
			sl.Range = 20.0f;
			e.AddComponent<VisibilityComponent>();
		};
		const auto addMesh = [](const uint64_t meshHandle)
		{
			return [meshHandle](Entity e, const glm::vec3&)
			{
				e.AddComponent<MeshComponent>().MeshHandle = AssetHandle{meshHandle};
				e.AddComponent<MaterialComponent>().Material = AssetHandle{kWhiteMaterialHandle};
				e.AddComponent<VisibilityComponent>();
			};
		};

		// One flat, icon-prefixed row. Returns true if clicked (so the caller closes its popup).
		const ImU32 accent = EditorTheme::AccentColor();
		const auto row = [&](CreateIcon icon, const char* label) -> bool
		{
			DrawCreateIcon(icon, accent);
			return ImGui::Selectable(label);
		};

		// The menu body, shared verbatim by the primary-button popup and the right-click context menu.
		const auto drawCreateItems = [&]()
		{
			EditorTheme::SectionHeader("Create");
			if (row(CreateIcon::Empty, "Empty Entity"))
				createPreset("Empty", [](Entity, const glm::vec3&) {});
			if (row(CreateIcon::Directional, "Directional Light"))
				createPreset("Directional Light", addDirectional);
			if (row(CreateIcon::Point, "Point Light"))
				createPreset("Point Light", addPoint);
			if (row(CreateIcon::Spot, "Spot Light"))
				createPreset("Spot Light", addSpot);
			if (row(CreateIcon::Cube, "Cube"))
				createPreset("Cube", addMesh(kCubeMeshHandle));
			if (row(CreateIcon::Cube, "Plane"))
				createPreset("Plane", addMesh(kQuadMeshHandle));
		};

		// Primary amber command-console action + its dropdown.
		if (EditorTheme::PrimaryButton("+ Create"))
		{
			ImGui::OpenPopup("##createmenu");
		}
		if (ImGui::BeginPopup("##createmenu"))
		{
			drawCreateItems();
			ImGui::EndPopup();
		}
		ImGui::Separator();

		// Only entities that are part of the scene model
		auto view = m_World->GetRegistry().view<IDComponent, TagComponent>();

		for (const entt::entity e : view)
		{
			Entity entity{e, m_World};
			DrawEntityNode(entity);
		}

		// Right-click empty hierarchy space -> the same Create menu (Unity/Unreal both do this). Window
		// context popup so it only opens over empty space, not over an entity row (rows have their own menu).
		// Reuses drawCreateItems verbatim so the button and right-click never drift.
		if (ImGui::BeginPopupContextWindow("##createcontext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			drawCreateItems();
			ImGui::EndPopup();
		}

		// Left-clicking empty space clears selection (right-click is reserved for the Create menu above).
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
		else if (const auto& sel = m_World->GetSingleton<EditorSelectionSingleton>();
		         sel.SelectedAssetType == AssetType::Material && sel.SelectedAsset != 0)
		{
			// A material asset is selected in the Content Browser — show the material inspector instead of
			// the entity component inspector (the two are mutually exclusive; see EditorSelectionSingleton).
			DrawMaterialInspector(*m_World, m_World->GetSingleton<AssetManagerSingleton>(), sel.SelectedAsset);
		}
		else
		{
			EditorTheme::WarningBanner("NOTHING SELECTED");
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

		// Double-click focuses the camera on the entity (same as pressing F with it selected).
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			SetSelected(entity);
			FrameCameraOnEntity(*m_World, entity.Handle());
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
			// Structural entities (viewport, cameras) can't be deleted — show the item disabled.
			if (ImGui::MenuItem("Delete", nullptr, false, IsDeletable(entity)))
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
