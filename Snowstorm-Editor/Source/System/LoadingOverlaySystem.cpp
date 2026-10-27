#include "LoadingOverlaySystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Systems/LoadProgress.hpp"

#include <imgui.h>

namespace Snowstorm
{
	void LoadingOverlaySystem::Execute(Timestep)
	{
		auto& assets = SingletonView<AssetManagerSingleton>();
		auto& shaderLib = Application::Get().GetServiceManager().GetService<ShaderLibrary>();

		const uint32_t assetPending = assets.PendingLoadCount();
		const uint32_t shaderPending = shaderLib.PendingCompileCount();

		if (assetPending == 0 && shaderPending == 0)
		{
			return; // nothing loading or compiling — no overlay
		}

		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 center = {vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f};

		ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
		ImGui::SetNextWindowBgAlpha(0.85f);

		constexpr ImGuiWindowFlags flags =
		    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

		const auto drawBar = [](const char* label, const uint32_t pending, const uint32_t total)
		{
			const LoadProgress p = MakeLoadProgress(pending, total);
			if (p.Total == 0)
			{
				return;
			}
			ImGui::Text("%s %u / %u", label, p.Done, p.Total);
			ImGui::ProgressBar(p.Fraction, ImVec2(260.0f, 0.0f));
		};

		if (ImGui::Begin("##LoadingOverlay", nullptr, flags))
		{
			// Shaders first: on a cold cache they're why the editor used to go white, and they gate whether
			// anything can draw at all, so surface them above asset streaming.
			if (shaderPending > 0)
			{
				drawBar("Compiling shaders...", shaderPending, shaderLib.PendingCompileTotal());
			}
			if (assetPending > 0)
			{
				drawBar("Loading assets...", assetPending, assets.PendingLoadTotal());
			}
		}
		ImGui::End();
	}
}
