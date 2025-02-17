#include "EditorMenuSystem.hpp"

#include <imgui.h>

#include "Snowstorm/Core/Application.hpp"

namespace Snowstorm
{
	void EditorMenuSystem::Execute(Timestep ts)
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
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
