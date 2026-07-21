#include "StatusBarSystem.hpp"

#include "Singletons/EditorStatusBarSingleton.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Singletons/EditorSelectionSingleton.hpp"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar

#include <filesystem>
#include <string>

namespace Snowstorm
{
	void StatusBarSystem::Execute(const Timestep ts)
	{
		auto& status = SingletonView<EditorStatusBarSingleton>();
		status.TickMessage(ts.GetSeconds());

		ImGuiViewport* viewport = ImGui::GetMainViewport();

		// A bar pinned to the bottom of the main viewport. BeginViewportSideBar reserves the strip from
		// the viewport work-area (so the dockspace, sized to WorkSize afterwards, doesn't overlap it).
		const float height = ImGui::GetFrameHeight();
		// MenuBar flag so we can lay items out in a BeginMenuBar strip (the idiomatic status-bar layout).
		constexpr ImGuiWindowFlags flags =
		    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

		if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags))
		{
			if (ImGui::BeginMenuBar())
			{
				// --- Left: scene file + unsaved-changes marker (the always-relevant context). ---
				const std::string& scenePath = status.GetScenePath();
				const std::string sceneName = scenePath.empty()
				                                  ? "<unsaved>"
				                                  : std::filesystem::path(scenePath).filename().string();
				ImGui::Text("%s%s", sceneName.c_str(), status.IsDirty() ? " *" : "");

				// Transient last-action message, right after the scene (fades via TickMessage).
				if (status.GetMessageTime() > 0.0f && !status.GetMessage().empty())
				{
					ImGui::TextDisabled("|");
					ImGui::TextUnformatted(status.GetMessage().c_str());
				}

				// --- Center: current selection. ---
				if (const Entity selected = SingletonView<EditorSelectionSingleton>().Selected;
				    selected && selected.HasComponent<TagComponent>())
				{
					const std::string sel = "Selected: " + selected.GetComponent<TagComponent>().Tag;
					// Roughly center the label in the bar.
					const float mid = viewport->WorkSize.x * 0.5f - ImGui::CalcTextSize(sel.c_str()).x * 0.5f;
					if (mid > ImGui::GetCursorPosX())
					{
						ImGui::SetCursorPosX(mid);
					}
					ImGui::TextUnformatted(sel.c_str());
				}

				// --- Right: FPS, right-aligned. ---
				char fps[32];
				std::snprintf(fps, sizeof(fps), "%.0f FPS", ImGui::GetIO().Framerate);
				const float rightX = viewport->WorkSize.x - ImGui::CalcTextSize(fps).x - ImGui::GetStyle().FramePadding.x * 2.0f;
				if (rightX > ImGui::GetCursorPosX())
				{
					ImGui::SetCursorPosX(rightX);
				}
				ImGui::TextUnformatted(fps);

				ImGui::EndMenuBar();
			}
		}
		ImGui::End();
	}
}
