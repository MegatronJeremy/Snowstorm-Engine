#include "ViewportDisplaySystem.hpp"

#include <imgui.h>

#include "Snowstorm/World/Components.hpp"

namespace Snowstorm
{
	void ViewportDisplaySystem::Execute(Timestep ts)
	{
		const auto viewportView = View<ViewportComponent, FramebufferComponent>();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
		ImGui::Begin("Viewport");

		for (const auto entity : viewportView)
		{
			auto [viewportComponent, framebufferComponent] = viewportView.get(entity);
			viewportComponent.Focused = ImGui::IsWindowFocused();
			viewportComponent.Hovered = ImGui::IsWindowHovered();

			// Application::BlockEvents(!viewportComponent.Focused || !viewportComponent.Hovered);

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			viewportComponent.Size = {viewportPanelSize.x, viewportPanelSize.y};

			const uint32_t textureID = framebufferComponent.Framebuffer->GetColorAttachmentRendererID();
			ImGui::Image(reinterpret_cast<ImTextureID>(textureID),
			             ImVec2{viewportComponent.Size.x, viewportComponent.Size.y}, ImVec2{0, 1},
			             ImVec2{1, 0});
			ImGui::End();
			ImGui::PopStyleVar();

			// TODO make some sort of access view -> just accesses one (first) entity from the view
		}
	}
}
