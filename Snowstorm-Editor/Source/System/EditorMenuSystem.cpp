#include "EditorMenuSystem.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Singletons/EditorStatusBarSingleton.hpp"

#include <imgui.h>

#include <cstring>
#include <string>

namespace Snowstorm
{
	namespace
	{
		// Undo/redo, reporting what happened on the ambient status bar (not a toast — undo/redo is
		// high-frequency and a popup per keystroke is noise). The command name MUST be read before the
		// op runs: Undo moves the command onto the redo stack, so PeekUndoName would then return the
		// *next* one. Shared by the keyboard shortcuts and the Edit menu so message + ordering match.
		void UndoAction(EditorHistorySingleton& history, World& world, EditorStatusBarSingleton& status)
		{
			if (!history.CanUndo())
			{
				status.SetMessage("Nothing to undo");
				return;
			}
			const char* name = history.PeekUndoName();
			const std::string label = name ? (std::string("Undo: ") + name) : "Undo";
			history.Undo(world);
			status.SetMessage(label);
		}

		void RedoAction(EditorHistorySingleton& history, World& world, EditorStatusBarSingleton& status)
		{
			if (!history.CanRedo())
			{
				status.SetMessage("Nothing to redo");
				return;
			}
			const char* name = history.PeekRedoName();
			const std::string label = name ? (std::string("Redo: ") + name) : "Redo";
			history.Redo(world);
			status.SetMessage(label);
		}
	}

	void EditorMenuSystem::Execute(Timestep)
	{
		auto& cmds = SingletonView<EditorCommandsSingleton>();
		auto& input = SingletonView<InputStateSingleton>();
		auto& notify = SingletonView<EditorNotificationsSingleton>();

		// Ctrl+S (edge-triggered)
		const bool ctrlDown =
		    input.Down.test(Key::LeftControl) || input.Down.test(Key::RightControl);

		const bool savePressed =
		    ctrlDown && input.PressedThisFrame.test(Key::S);

		// Optional: don't fire while typing in an input field
		if (savePressed && !input.WantCaptureKeyboard)
		{
			if (cmds.SaveScene)
			{
				const bool ok = cmds.SaveScene(); // make it return bool (recommended)
				notify.Push(ok ? "Scene saved" : "Save failed", ok ? EditorToastType::Success : EditorToastType::Error);
			}
			else
			{
				notify.Push("SaveScene not bound", EditorToastType::Warning);
			}
		}

		// Undo / Redo (edge-triggered, not while typing). Redo = Ctrl+Y or Ctrl+Shift+Z (Ctrl+R is the
		// Scale-gizmo key, so it is deliberately not used here).
		auto& history = SingletonView<EditorHistorySingleton>();
		auto& status = SingletonView<EditorStatusBarSingleton>();
		if (ctrlDown && !input.WantCaptureKeyboard)
		{
			const bool shift = input.Down.test(Key::LeftShift) || input.Down.test(Key::RightShift);

			if (input.PressedThisFrame.test(Key::Z) && !shift)
			{
				UndoAction(history, *m_World, status);
			}
			else if (input.PressedThisFrame.test(Key::Y) || (input.PressedThisFrame.test(Key::Z) && shift))
			{
				RedoAction(history, *m_World, status);
			}
		}

		// --- UI
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				const bool canSave = static_cast<bool>(cmds.SaveScene);

				// Show shortcut text, but action is handled by ECS input above too
				if (ImGui::MenuItem("Save", "Ctrl+S", false, canSave))
				{
					if (cmds.SaveScene)
					{
						const bool ok = cmds.SaveScene();
						notify.Push(ok ? "Scene saved" : "Save failed", ok ? EditorToastType::Success : EditorToastType::Error);
					}
				}

				if (ImGui::MenuItem("Import Model..."))
				{
					m_ShowImportPopup = true;
				}

				ImGui::Separator();

				if (ImGui::MenuItem("Exit"))
				{
					Application::Get().Close();
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				const char* undoName = history.PeekUndoName();
				const char* redoName = history.PeekRedoName();
				const std::string undoLabel = undoName ? (std::string("Undo ") + undoName) : "Undo";
				const std::string redoLabel = redoName ? (std::string("Redo ") + redoName) : "Redo";

				if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, history.CanUndo()))
				{
					UndoAction(history, *m_World, status);
				}
				if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, history.CanRedo()))
				{
					RedoAction(history, *m_World, status);
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				if (ImGui::MenuItem("Keyboard & Mouse Shortcuts", nullptr, m_ShowShortcuts))
				{
					m_ShowShortcuts = !m_ShowShortcuts;
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		DrawImportModelPopup(notify);
		DrawShortcutsWindow();

		ImGui::End();
	}

	void EditorMenuSystem::DrawShortcutsWindow()
	{
		if (!m_ShowShortcuts)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Keyboard & Mouse Shortcuts", &m_ShowShortcuts))
		{
			ImGui::End();
			return;
		}

		// A two-column key/action table. Keep these rows in sync with the real bindings (CLAUDE.md).
		const auto section = [](const char* title)
		{
			ImGui::Spacing();
			ImGui::SeparatorText(title);
		};

		const auto row = [](const char* keys, const char* action)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(keys);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action);
		};

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;

		ImGui::TextDisabled("Camera navigation requires holding the right mouse button in the viewport.");

		section("Camera (hold Right Mouse Button in viewport)");
		if (ImGui::BeginTable("cam", 2, tableFlags))
		{
			row("Right Mouse (hold)", "Look around / enable fly controls");
			row("W / A / S / D", "Move forward / left / back / right");
			row("E / Q", "Move up / down");
			row("Left Shift", "Sprint (move faster)");
			row("Left Ctrl", "Move slower (precision)");
			row("Mouse Wheel (RMB held)", "Adjust fly speed");
			row("Mouse Wheel (no RMB)", "Zoom / dolly (perspective) or zoom (ortho)");
			ImGui::EndTable();
		}

		section("Selection & Gizmos");
		if (ImGui::BeginTable("sel", 2, tableFlags))
		{
			row("Left Mouse (viewport)", "Select entity under cursor");
			row("W", "Translate gizmo");
			row("E", "Rotate gizmo");
			row("R", "Scale gizmo");
			ImGui::EndTable();
		}

		section("Camera Framing");
		if (ImGui::BeginTable("frame", 2, tableFlags))
		{
			row("F", "Frame selected entity (or whole scene if none)");
			row("Shift + F", "Frame the whole scene");
			row("Double-click entity", "Focus camera on it (hierarchy or viewport)");
			ImGui::EndTable();
		}

		section("Edit");
		if (ImGui::BeginTable("edit", 2, tableFlags))
		{
			row("Ctrl + Z", "Undo");
			row("Ctrl + Y", "Redo");
			row("Ctrl + Shift + Z", "Redo (alternate)");
			ImGui::EndTable();
		}

		section("Scene");
		if (ImGui::BeginTable("scene", 2, tableFlags))
		{
			row("Ctrl + S", "Save current scene");
			ImGui::EndTable();
		}

		section("Scene Hierarchy");
		if (ImGui::BeginTable("hier", 2, tableFlags))
		{
			row("Delete", "Delete the selected entity");
			row("Right-click > Rename", "Rename the entity");
			row("Right-click > Duplicate", "Duplicate the entity");
			row("Right-click > Delete", "Delete the entity");
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void EditorMenuSystem::DrawImportModelPopup(EditorNotificationsSingleton& notify)
	{
		// Opening must happen outside the menu (the menu closes on click); flag-then-open here.
		if (m_ShowImportPopup)
		{
			ImGui::OpenPopup("Import Model");
			m_ShowImportPopup = false;
		}

		// Center the modal.
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Path to model file (relative to repo root):");
			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputText("##importpath", m_ImportPathBuffer, sizeof(m_ImportPathBuffer));
			ImGui::TextDisabled("e.g. assets/meshes/sponza.obj  (.obj/.fbx/.gltf)");

			ImGui::Separator();

			const bool hasPath = m_ImportPathBuffer[0] != '\0';
			if (!hasPath)
				ImGui::BeginDisabled();
			if (ImGui::Button("Import", ImVec2(120.0f, 0.0f)))
			{
				auto& assets = SingletonView<AssetManagerSingleton>();
				const std::vector<Entity> created = assets.ImportModel(m_ImportPathBuffer);
				if (!created.empty())
				{
					notify.Push("Imported " + std::to_string(created.size()) + " parts", EditorToastType::Success);
					m_ImportPathBuffer[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
				else
				{
					notify.Push("Import failed (see log)", EditorToastType::Error);
				}
			}
			if (!hasPath)
				ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}
