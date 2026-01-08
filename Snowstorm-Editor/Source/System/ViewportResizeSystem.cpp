#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"

namespace Snowstorm
{
	void ViewportResizeSystem::Execute(Timestep ts)
	{
		const auto viewportView = View<ViewportComponent, RenderTargetComponent>();
		const auto cameraInitView = InitView<CameraComponent, RenderTargetComponent>();
		const auto cameraView = View<CameraComponent, RenderTargetComponent>();

		// Get application window size
		const Application& app = Application::Get();
		const uint32_t windowWidth = app.GetWindow().GetWidth();
		const uint32_t windowHeight = app.GetWindow().GetHeight();

		// Check if ImGui is active
		const bool isImGuiEnabled = app.GetServiceManager().ServiceRegistered<ImGuiService>();

		for (const auto entity : viewportView)
		{
			auto [viewport, renderTarget] = viewportView.get<ViewportComponent, RenderTargetComponent>(entity);

			// Determine viewport size
			uint32_t viewportWidth, viewportHeight;

			if (isImGuiEnabled)
			{
				// Use ImGui's viewport size
				viewportWidth = static_cast<uint32_t>(viewport.Size.x);
				viewportHeight = static_cast<uint32_t>(viewport.Size.y);

				if (viewportWidth < 64 || viewportHeight < 64)
				{
					continue; 
				}
			}
			else
			{
				// Render to full framebuffer when ImGui is not enabled
				viewport.Size = {static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
				viewportWidth = windowWidth;
				viewportHeight = windowHeight;
			}

			auto resizeCameraRt = [&](const entt::entity cameraEntity)
			{
				if (auto [camera, rt] = cameraView.get<CameraComponent, RenderTargetComponent>(cameraEntity);
					rt.TargetEntity == entity)
				{
					camera.Camera.SetViewportSize(viewportWidth, viewportHeight);
				}
			};

			// Set the initial viewport size for all new cameras
			for (const auto& cameraEntity : cameraInitView)
			{
				resizeCameraRt(cameraEntity);
			}

			// Check if the framebuffer already matches the new size
			const auto& rtDesc = renderTarget.Target->GetDesc();
			if (rtDesc.Width == viewportWidth && rtDesc.Height == viewportHeight)
			{
				continue;
			}

			renderTarget.Target->Resize(viewportWidth, viewportHeight);

			// Resize all resized camera viewports within the framebuffer
			for (const auto& cameraEntity : cameraView)
			{
				resizeCameraRt(cameraEntity);
			}
		}
	}
}
