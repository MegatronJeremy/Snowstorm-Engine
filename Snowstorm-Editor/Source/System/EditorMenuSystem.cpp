#include "EditorMenuSystem.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

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

				ImGui::Separator();

				if (ImGui::MenuItem("Exit"))
				{
					Application::Get().Close();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}
		ImGui::End();
	}
}
