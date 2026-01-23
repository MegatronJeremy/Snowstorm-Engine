#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"

namespace Snowstorm
{
	namespace
	{
		bool ValidViewportSize(uint32_t w, uint32_t h)
		{
			return w >= 64 && h >= 64;
		}
	}

	void ViewportResizeSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();

		const auto viewportView = View<ViewportComponent, RenderTargetComponent>();
		const auto changedViewports = ChangedView<ViewportComponent>();

		const auto cameraInit = InitView<CameraComponent, CameraTargetComponent>();

		const Application& app = Application::Get();
		const bool isImGuiEnabled = app.GetServiceManager().ServiceRegistered<ImGuiService>();

		// If you want: when no viewport changed, you can still init new cameras and early-out.
		const bool anyViewportChanged = !changedViewports.empty();
		if (!anyViewportChanged && cameraInit.empty())
		{
			return;
		}

		for (const entt::entity vpEntity : viewportView)
		{
			// If ImGui is enabled, we resize only when the viewport size changed.
			// If ImGui is disabled, the viewport matches the window each frame (so treat as changed).
			if (isImGuiEnabled && !changedViewports.contains(vpEntity) && cameraInit.empty())
			{
				continue;
			}

			const auto& vp = reg.Read<ViewportComponent>(vpEntity);

			uint32_t w = static_cast<uint32_t>(vp.Size.x);
			uint32_t h = static_cast<uint32_t>(vp.Size.y);

			if (isImGuiEnabled)
			{
				if (!ValidViewportSize(w, h))
				{
					continue;
				}
			}
			else
			{
				// Non-imgui path: viewport = window size
				const uint32_t windowW = app.GetWindow().GetWidth();
				const uint32_t windowH = app.GetWindow().GetHeight();

				// mark viewport as changed
				auto& vpW = reg.Write<ViewportComponent>(vpEntity);
				vpW.Size = {static_cast<float>(windowW), static_cast<float>(windowH)};
				w = windowW;
				h = windowH;
			}

			// Rebuild RT only when necessary.
			{
				const auto& rt = reg.Read<RenderTargetComponent>(vpEntity);
				if (!rt.Target)
				{
					auto& rtW = reg.Write<RenderTargetComponent>(vpEntity);
					rtW.Target = CreateDefaultSceneRenderTarget(w, h, "Viewport");
				}
				else
				{
					const auto& desc = rt.Target->GetDesc();
					if (desc.Width != w || desc.Height != h)
					{
						auto& rtW = reg.Write<RenderTargetComponent>(vpEntity);
						rtW.Target = CreateDefaultSceneRenderTarget(w, h, "Viewport");
					}
				}
			}
		}
	}
}