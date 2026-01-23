#include "EditorNotificationSystem.hpp"

#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

namespace Snowstorm
{
	void EditorNotificationSystem::Execute(const Timestep ts)
	{
		auto& notify = SingletonView<EditorNotificationsSingleton>();
		auto& toasts = notify.Get();

		// Update lifetimes
		const float dt = ts.GetSeconds();
		for (auto& t : toasts)
			t.TimeRemaining -= dt;

		toasts.erase(std::ranges::remove_if(toasts,
		                                    [](const EditorToast& t) { return t.TimeRemaining <= 0.0f; }).begin(),
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
				// minimal styling: prefix
				const char* prefix =
				(t.Type == EditorToastType::Success) ? "[OK] " : (t.Type == EditorToastType::Warning) ? "[!] " : (t.Type == EditorToastType::Error) ? "[X] " : "[i] ";

				ImGui::Text("%s%s", prefix, t.Text.c_str());
			}
		}
		ImGui::End();
	}
}
