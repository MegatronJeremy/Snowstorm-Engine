#include "EditorMenuSystem.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

#include <cstring>

namespace Snowstorm
{
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

			if (ImGui::BeginMenu("Scene"))
			{
				const bool canBuild = static_cast<bool>(cmds.BuildStressScene);
				if (ImGui::MenuItem("Build Stress Scene", nullptr, false, canBuild))
				{
					cmds.BuildStressScene();
					notify.Push("Stress scene built", EditorToastType::Success);
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		DrawImportModelPopup(notify);

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
