#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/FramebufferComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"

namespace Snowstorm
{
	void ViewportResizeSystem::Execute(Timestep ts)
	{
		const auto viewportView = View<ViewportComponent, FramebufferComponent>();
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
			auto [viewport, framebuffer] = viewportView.get<ViewportComponent, FramebufferComponent>(entity);

			// Determine viewport size
			uint32_t viewportWidth, viewportHeight;

			if (isImGuiEnabled)
			{
				// Use ImGui's viewport size
				viewportWidth = static_cast<uint32_t>(viewport.Size.x);
				viewportHeight = static_cast<uint32_t>(viewport.Size.y);
			}
			else
			{
				// Render to full framebuffer when ImGui is not enabled
				viewport.Size = {static_cast<float>(windowWidth), static_cast<float>(windowHeight)};
				viewportWidth = windowWidth;
				viewportHeight = windowHeight;
			}

			auto resizeCameraFb = [&](const entt::entity cameraEntity)
			{
				if (auto [camera, renderTarget] = cameraView.get<CameraComponent, RenderTargetComponent>(cameraEntity);
					renderTarget.TargetFramebuffer == entity)
				{
					camera.Camera.SetViewportSize(viewportWidth, viewportHeight);
				}
			};

			// Set initial viewport size for all new cameras
			for (const auto& cameraEntity : cameraInitView)
			{
				resizeCameraFb(cameraEntity);
			}

			// Check if framebuffer already matches the new size
			const auto& fbSpec = framebuffer.Framebuffer->GetSpecification();
			if (fbSpec.Width == viewportWidth && fbSpec.Height == viewportHeight)
			{
				continue;
			}

			framebuffer.Framebuffer->Resize(viewportWidth, viewportHeight);

			// Resize all resized camera viewports within the framebuffer
			for (const auto& cameraEntity : cameraView)
			{
				resizeCameraFb(cameraEntity);
			}
		}
	}
}
