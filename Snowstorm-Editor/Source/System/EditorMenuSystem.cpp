#include "EditorMenuSystem.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"

#include "System/ConsoleSystem.hpp"
#include "System/CVarPanelSystem.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Utility/FileDialog.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Singletons/EditorStatusBarSingleton.hpp"

#include <imgui.h>

#include <cstring>
#include <filesystem>
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

		const bool ctrlDown =
		    input.Down.test(Key::LeftControl) || input.Down.test(Key::RightControl);
		const bool shiftDown =
		    input.Down.test(Key::LeftShift) || input.Down.test(Key::RightShift);

		// Scene file shortcuts (edge-triggered, suppressed while typing). Hazel's bindings:
		// Ctrl+N new scene, Ctrl+O open scene, Ctrl+S save, Ctrl+Shift+S save as. Shared with the
		// File menu below so behavior + toasts match. Keep DrawShortcutsWindow in sync (CLAUDE.md).
		if (ctrlDown && !input.WantTextInput)
		{
			if (input.PressedThisFrame.test(Key::S) && !shiftDown)
			{
				SaveSceneAction(notify);
			}
			else if (input.PressedThisFrame.test(Key::S) && shiftDown)
			{
				SaveSceneAsAction(notify);
			}
			else if (input.PressedThisFrame.test(Key::N))
			{
				if (cmds.NewScene)
				{
					cmds.NewScene();
					notify.Push("New scene", EditorToastType::Info);
				}
			}
			else if (input.PressedThisFrame.test(Key::O))
			{
				OpenSceneAction(notify);
			}
		}

		// Undo / Redo (edge-triggered, not while typing). Redo = Ctrl+Y or Ctrl+Shift+Z (Ctrl+R is the
		// Scale-gizmo key, so it is deliberately not used here).
		auto& history = SingletonView<EditorHistorySingleton>();
		auto& status = SingletonView<EditorStatusBarSingleton>();
		if (ctrlDown && !input.WantTextInput)
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

		// Backtick toggles the developer console (the classic Quake/Unreal console key). Edge-triggered and
		// suppressed while typing, so pressing ` inside the console's own input types a character instead of
		// closing it.
		if (input.PressedThisFrame.test(Key::GraveAccent) && !input.WantTextInput)
		{
			ConsoleSystem::s_Open = !ConsoleSystem::s_Open;
		}

		// --- UI
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				// New/Open hand off to EditorLayer via EditorCommandsSingleton — the menu only asks
				// "where" (+ "what name" for New), never touches Project/World state directly.
				if (ImGui::MenuItem("New Project..."))
				{
					// Opening must happen outside the menu (the menu closes on click) — see
					// DrawNewProjectPopup, same flag-then-open pattern as the Import Model popup.
					m_ShowNewProjectPopup = true;
				}

				if (ImGui::MenuItem("Open Project..."))
				{
					if (const std::filesystem::path ssprojPath = FileDialog::OpenFile({{"Snowstorm Project", "ssproj"}}); !ssprojPath.empty() && cmds.OpenProject)
					{
						const bool ok = cmds.OpenProject(ssprojPath);
						notify.Push(ok ? "Project opened" : "Failed to open project", ok ? EditorToastType::Success : EditorToastType::Error);
					}
				}

				if (ImGui::MenuItem("Save Project", nullptr, false, static_cast<bool>(cmds.SaveProject)))
				{
					if (cmds.SaveProject)
					{
						const bool ok = cmds.SaveProject();
						notify.Push(ok ? "Project saved" : "Save failed", ok ? EditorToastType::Success : EditorToastType::Error);
					}
				}

				ImGui::Separator();

				if (ImGui::MenuItem("New Scene", "Ctrl+N", false, static_cast<bool>(cmds.NewScene)))
				{
					cmds.NewScene();
					notify.Push("New scene", EditorToastType::Info);
				}

				if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, static_cast<bool>(cmds.OpenScene)))
				{
					OpenSceneAction(notify);
				}

				const bool canSave = static_cast<bool>(cmds.SaveScene);

				// Show shortcut text, but action is handled by ECS input above too
				if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSave))
				{
					SaveSceneAction(notify);
				}

				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, static_cast<bool>(cmds.SaveSceneAs)))
				{
					SaveSceneAsAction(notify);
				}

				ImGui::Separator();

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

			if (ImGui::BeginMenu("Debug"))
			{
				if (ImGui::MenuItem("Console", "`", ConsoleSystem::s_Open))
				{
					ConsoleSystem::s_Open = !ConsoleSystem::s_Open;
				}
				if (ImGui::MenuItem("Console Variables", nullptr, CVarPanelSystem::s_Open))
				{
					CVarPanelSystem::s_Open = !CVarPanelSystem::s_Open;
				}
				ImGui::Separator();

				// Chrome-tracing capture: records the next N frames (main thread + JobSystem workers) to a
				// JSON openable in chrome://tracing / ui.perfetto.dev. Disabled while a capture is in flight.
				const bool capturing = Instrumentor::Get().IsCapturing() || Instrumentor::Get().IsCapturePending();
				if (ImGui::MenuItem("Capture 1 Frame", nullptr, false, !capturing))
				{
					Instrumentor::Get().RequestCapture(1, "SnowstormCapture.json");
					notify.Push("Profiler: capturing 1 frame -> SnowstormCapture.json", EditorToastType::Info);
				}
				if (ImGui::MenuItem("Capture 10 Frames", nullptr, false, !capturing))
				{
					Instrumentor::Get().RequestCapture(10, "SnowstormCapture.json");
					notify.Push("Profiler: capturing 10 frames -> SnowstormCapture.json", EditorToastType::Info);
				}
				if (capturing)
				{
					ImGui::TextDisabled("capturing...");
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
		DrawNewProjectPopup(notify);
		DrawShortcutsWindow();

		ImGui::End();
	}

	void EditorMenuSystem::DrawShortcutsWindow()
	{
		if (!m_ShowShortcuts)
		{
			return;
		}

		// Center it each time it opens (it's a transient reference dialog, not a dockable panel — so unlike
		// the console/CVar panels it shouldn't dock; it should just appear front-and-center like a dialog).
		// ImGuiCond_Appearing re-centers on every toggle-on while still letting the user drag it meanwhile.
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
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
			row("Escape", "Clear selection (hides the gizmo)");
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
			row("Ctrl + N", "New (empty) scene");
			row("Ctrl + O", "Open scene...");
			row("Ctrl + S", "Save current scene");
			row("Ctrl + Shift + S", "Save scene as...");
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

		section("Debug / Console");
		if (ImGui::BeginTable("debug", 2, tableFlags))
		{
			row("`  (backtick)", "Toggle the developer console");
			row("Tab (in console)", "Autocomplete command / CVar (repeat to cycle)");
			row("Up / Down (in console)", "Cycle command history");
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void EditorMenuSystem::OpenSceneAction(EditorNotificationsSingleton& notify)
	{
		auto& cmds = SingletonView<EditorCommandsSingleton>();
		const std::filesystem::path scenesDir = Project::GetActive() ? Project::GetActive()->GetAssetDirectory() / "scenes" : std::filesystem::path{};
		if (const std::filesystem::path path = FileDialog::OpenFile({{"Snowstorm Scene", "world"}}, scenesDir);
		    !path.empty() && cmds.OpenScene)
		{
			const bool ok = cmds.OpenScene(path.string());
			notify.Push(ok ? "Opened " + path.filename().string() : "Failed to open scene",
			            ok ? EditorToastType::Success : EditorToastType::Error);
		}
	}

	void EditorMenuSystem::SaveSceneAction(EditorNotificationsSingleton& notify)
	{
		auto& cmds = SingletonView<EditorCommandsSingleton>();
		// An untitled scene (fresh New Scene) has no file yet, so Save falls through to Save As — the native
		// "where to save" dialog — instead of failing with "no active scene path" (standard editor behavior).
		if (cmds.HasScenePath && !cmds.HasScenePath())
		{
			SaveSceneAsAction(notify);
			return;
		}
		if (cmds.SaveScene)
		{
			const bool ok = cmds.SaveScene();
			notify.Push(ok ? "Scene saved" : "Save failed", ok ? EditorToastType::Success : EditorToastType::Error);
		}
	}

	void EditorMenuSystem::SaveSceneAsAction(EditorNotificationsSingleton& notify)
	{
		auto& cmds = SingletonView<EditorCommandsSingleton>();
		const std::filesystem::path scenesDir = Project::GetActive() ? Project::GetActive()->GetAssetDirectory() / "scenes" : std::filesystem::path{};
		if (const std::filesystem::path path = FileDialog::SaveFile({{"Snowstorm Scene", "world"}}, scenesDir);
		    !path.empty() && cmds.SaveSceneAs)
		{
			const bool ok = cmds.SaveSceneAs(path.string());
			notify.Push(ok ? "Saved " + path.filename().string() : "Save failed",
			            ok ? EditorToastType::Success : EditorToastType::Error);
		}
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
			ImGui::TextUnformatted("Model file to import into the project:");
			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputText("##importpath", m_ImportPathBuffer, sizeof(m_ImportPathBuffer));

			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				if (const std::filesystem::path picked = FileDialog::OpenFile(
				        {{"Wavefront OBJ", "obj"}, {"FBX", "fbx"}, {"glTF", "gltf"}, {"glTF Binary", "glb"}});
				    !picked.empty())
				{
					const std::string pickedStr = picked.string();
					strncpy_s(m_ImportPathBuffer, pickedStr.c_str(), sizeof(m_ImportPathBuffer) - 1);
				}
			}

			ImGui::TextDisabled("The model and its textures are copied into the project's assets/meshes/ folder.");

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

	void EditorMenuSystem::DrawNewProjectPopup(EditorNotificationsSingleton& notify)
	{
		if (m_ShowNewProjectPopup)
		{
			ImGui::OpenPopup("New Project");
			m_ShowNewProjectPopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputTextWithHint("##new_project_name", "Project Name", m_NewProjectNameBuffer, sizeof(m_NewProjectNameBuffer));

			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputTextWithHint("##new_project_location", "Project Location", m_NewProjectLocationBuffer, sizeof(m_NewProjectLocationBuffer));

			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				if (const std::filesystem::path picked = FileDialog::OpenFolder(); !picked.empty())
				{
					const std::string pickedStr = picked.string();
					strncpy_s(m_NewProjectLocationBuffer, pickedStr.c_str(), sizeof(m_NewProjectLocationBuffer) - 1);
				}
			}

			// Full path preview, same as Hazel's — the project directory Create actually scaffolds is
			// Location/Name (a fresh subfolder), never the browsed folder itself.
			const bool hasName = m_NewProjectNameBuffer[0] != '\0';
			const bool hasLocation = m_NewProjectLocationBuffer[0] != '\0';
			const std::filesystem::path fullProjectPath = hasLocation && hasName
			                                                  ? std::filesystem::path(m_NewProjectLocationBuffer) / m_NewProjectNameBuffer
			                                                  : std::filesystem::path{};
			ImGui::TextDisabled("Full Project Path: %s", fullProjectPath.string().c_str());

			ImGui::Separator();

			if (!(hasName && hasLocation))
				ImGui::BeginDisabled();
			if (ImGui::Button("Create", ImVec2(120.0f, 0.0f)))
			{
				auto& cmds = SingletonView<EditorCommandsSingleton>();
				const bool ok = cmds.NewProject && cmds.NewProject(fullProjectPath, m_NewProjectNameBuffer);
				notify.Push(ok ? "Project created" : "Failed to create project", ok ? EditorToastType::Success : EditorToastType::Error);
				if (ok)
				{
					m_NewProjectNameBuffer[0] = '\0';
					m_NewProjectLocationBuffer[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}
			if (!(hasName && hasLocation))
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
