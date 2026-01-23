#include "ViewportDisplaySystem.hpp"

#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"

#include <imgui.h>

namespace Snowstorm
{
	void ViewportDisplaySystem::Execute(Timestep)
	{
		const auto viewportView = View<ViewportComponent, RenderTargetComponent>();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
		ImGui::Begin("Viewport");

		auto& reg = m_World->GetRegistry();

		for (const entt::entity e : viewportView)
		{
			// ---- Interaction flags
			reg.WriteIfChanged<ViewportInteractionComponent>(e, [&](auto& vi) // TODO this one is editor only, and can or can not exist
			{
				vi.Focused = ImGui::IsWindowFocused();
				vi.Hovered = ImGui::IsWindowHovered();
			});

			// ---- Viewport size
			const ImVec2 panelSize = ImGui::GetContentRegionAvail();
			reg.WriteIfChanged<ViewportComponent>(e, [&](auto& vp)
			{
				vp.Size = {panelSize.x, panelSize.y};
			});

			// ---- Draw
			const auto& rt = reg.Read<RenderTargetComponent>(e);
			if (!rt.Target)
			{
				continue;
			}

			const auto& desc = rt.Target->GetDesc();
			if (!desc.ColorAttachments.empty() && desc.ColorAttachments[0].View)
			{
				const uint64_t textureID = desc.ColorAttachments[0].View->GetUIID();
				const auto& vp = reg.Read<ViewportComponent>(e);

				ImGui::Image(
					textureID,
					ImVec2{vp.Size.x, vp.Size.y},
					ImVec2{0, 1},
					ImVec2{1, 0}
				);
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
