#include "ViewportDisplaySystem.hpp"

#include <imgui.h>

#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"

namespace Snowstorm
{
	void ViewportDisplaySystem::Execute(Timestep ts)
	{
		const auto viewportView = View<ViewportComponent, RenderTargetComponent>();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
		ImGui::Begin("Viewport");

		for (const auto entity : viewportView)
		{
			auto [viewportComponent, renderTargetComponent] = viewportView.get(entity);
			viewportComponent.Focused = ImGui::IsWindowFocused();
			viewportComponent.Hovered = ImGui::IsWindowHovered();

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			viewportComponent.Size = {viewportPanelSize.x, viewportPanelSize.y};

			const auto& desc = renderTargetComponent.Target->GetDesc();
			if (!desc.ColorAttachments.empty())
			{
				// TODO make some sort of access view -> just accesses one (first) entity from the view
				const uint64_t textureID = desc.ColorAttachments[0].View->GetUIID();
				ImGui::Image(textureID,
							 ImVec2{viewportComponent.Size.x, viewportComponent.Size.y}, ImVec2{0, 1},
							 ImVec2{1, 0});
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
