#include "EditorNotificationSystem.hpp"

#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Singletons/EditorStatusBarSingleton.hpp"

#include <imgui.h>

namespace Snowstorm
{
	void EditorNotificationSystem::Execute(const Timestep ts)
	{
		auto& notify = SingletonView<EditorNotificationsSingleton>();
		auto& toasts = notify.Get();

		// Route by severity (matches Unity/Unreal editor practice): routine confirmations the user just
		// triggered (Info/Success — "Scene saved", "Imported N parts") go to the bottom STATUS BAR, which
		// is calmer and consistently placed. Only Warning/Error stay as top-right TOASTS, since a failure
		// is worth interrupting for. Done here (not in Push) because only the system can reach both
		// singletons. Drain Info/Success into the status bar and drop them from the toast list.
		auto& statusBar = SingletonView<EditorStatusBarSingleton>();
		for (const auto& t : toasts)
		{
			if (t.Type == EditorToastType::Info || t.Type == EditorToastType::Success)
			{
				statusBar.SetMessage(t.Text);
			}
		}
		toasts.erase(std::ranges::remove_if(toasts, [](const EditorToast& t)
		                                    { return t.Type == EditorToastType::Info || t.Type == EditorToastType::Success; })
		                 .begin(),
		             toasts.end());

		// Update lifetimes (remaining toasts are Warning/Error only)
		const float dt = ts.GetSeconds();
		for (auto& t : toasts)
			t.TimeRemaining -= dt;

		toasts.erase(std::ranges::remove_if(toasts,
		                                    [](const EditorToast& t)
		                                    { return t.TimeRemaining <= 0.0f; })
		                 .begin(),
		             toasts.end());

		if (toasts.empty())
		{
			return;
		}

		// Draw top-right overlay
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 pos = {vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 12.0f};

		ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);

		constexpr ImGuiWindowFlags flags =
		    ImGuiWindowFlags_NoDecoration |
		    ImGuiWindowFlags_AlwaysAutoResize |
		    ImGuiWindowFlags_NoSavedSettings |
		    ImGuiWindowFlags_NoFocusOnAppearing |
		    ImGuiWindowFlags_NoNav;

		if (ImGui::Begin("##EditorToasts", nullptr, flags))
		{
			for (const auto& t : toasts)
			{
				// Only Warning/Error reach here (Info/Success were routed to the status bar above).
				const char* prefix = (t.Type == EditorToastType::Error) ? "[X] " : "[!] ";
				ImGui::Text("%s%s", prefix, t.Text.c_str());
			}
		}
		ImGui::End();
	}
}
