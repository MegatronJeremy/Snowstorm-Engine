#include "LoadingOverlaySystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"

#include <imgui.h>

namespace Snowstorm
{
	void LoadingOverlaySystem::Execute(Timestep)
	{
		auto& assets = SingletonView<AssetManagerSingleton>();

		const uint32_t pending = assets.PendingLoadCount();
		if (pending == 0)
		{
			return; // nothing loading — no overlay
		}

		const uint32_t total = assets.PendingLoadTotal();
		// total is the high-water mark since the queue was last empty; guard against a transient total==0.
		const float fraction = (total > 0) ? (1.0f - static_cast<float>(pending) / static_cast<float>(total)) : 0.0f;

		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 center = {vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f};

		ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
		ImGui::SetNextWindowBgAlpha(0.85f);

		constexpr ImGuiWindowFlags flags =
		    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin("##LoadingOverlay", nullptr, flags))
		{
			const uint32_t loaded = (total >= pending) ? (total - pending) : 0;
			ImGui::Text("Loading assets... %u / %u", loaded, total);
			ImGui::ProgressBar(fraction, ImVec2(260.0f, 0.0f));
		}
		ImGui::End();
	}
}
