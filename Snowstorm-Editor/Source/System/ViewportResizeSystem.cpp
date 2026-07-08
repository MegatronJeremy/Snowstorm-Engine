#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/Renderer.hpp"
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

			// Rebuild the HDR scene target + LDR present target together, only when missing or resized.
			{
				const auto& rt = reg.Read<RenderTargetComponent>(vpEntity);
				const bool missing = !rt.Target || !rt.PresentTarget || !rt.AAIntermediateTarget;
				const bool resized = rt.Target && (rt.Target->GetDesc().Width != w || rt.Target->GetDesc().Height != h);
				if (missing || resized)
				{
					// Drain the GPU before dropping the old targets: replacing the Ref destroys the VkImage/
					// view immediately, but in-flight frames may still be sampling them (the post-process pass
					// reads the scene target through the bindless array, and ImGui samples the present target).
					// Freeing a resource the GPU is mid-read of is a device-lost fault. Only on an actual
					// resize (`resized`), not first-time creation (`missing`) where nothing is in flight yet.
					// Matches VulkanContext::RecreateSwapchain, which likewise waits idle before teardown.
					if (resized)
					{
						Renderer::WaitIdle();
					}

					auto& rtW = reg.Write<RenderTargetComponent>(vpEntity);
					rtW.Target = CreateDefaultSceneRenderTarget(w, h, "Viewport");
					rtW.PresentTarget = CreatePresentTarget(w, h, "Viewport");
					rtW.PresentSampleView = CreatePresentSampleView(rtW.PresentTarget);
					// AA intermediate: same sRGB-store + UNORM-sample pair (FXAA renders present <- intermediate).
					// Always allocated (one extra RGBA8 target/viewport, negligible); the FXAA pass only uses it
					// when render.aa != 0.
					rtW.AAIntermediateTarget = CreatePresentTarget(w, h, "ViewportAA");
					rtW.AAIntermediateSampleView = CreatePresentSampleView(rtW.AAIntermediateTarget);
				}
			}
		}
	}
}