#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
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

			// Rebuild the scene + present + AA + upscale targets together, when missing, viewport-resized,
			// OR the internal render scale changed (which resizes only the low-res scene Target).
			{
				// Scene Target renders at render.scale (#43); everything downstream stays full viewport res.
				const float scale = CVars::ClampedRenderScale();
				const uint32_t sw = ScaledExtent(w, scale);
				const uint32_t sh = ScaledExtent(h, scale);

				const auto& rt = reg.Read<RenderTargetComponent>(vpEntity);
				const bool missing = !rt.Target || !rt.PresentTarget || !rt.AAIntermediateTarget || !rt.SceneUpscaleTarget;
				// Present target tracks the FULL viewport size; Target tracks the SCALED size. Compare each
				// against its own expected extent so a scale change (Target only) still triggers a rebuild.
				const bool viewportResized = rt.PresentTarget && (rt.PresentTarget->GetDesc().Width != w || rt.PresentTarget->GetDesc().Height != h);
				const bool scaleChanged = rt.Target && (rt.Target->GetDesc().Width != sw || rt.Target->GetDesc().Height != sh);
				if (missing || viewportResized || scaleChanged)
				{
					// Drain the GPU before dropping the old targets: replacing the Ref destroys the VkImage/
					// view immediately, but in-flight frames may still be sampling them (the post-process pass
					// reads the scene target through the bindless array, and ImGui samples the present target).
					// Freeing a resource the GPU is mid-read of is a device-lost fault. Only when replacing an
					// existing target (not first-time creation, where nothing is in flight yet).
					if (!missing)
					{
						Renderer::WaitIdle();
					}

					auto& rtW = reg.Write<RenderTargetComponent>(vpEntity);
					rtW.Target = CreateDefaultSceneRenderTarget(sw, sh, "Viewport"); // low-res when scale < 1
					rtW.PresentTarget = CreatePresentTarget(w, h, "Viewport");
					rtW.PresentSampleView = CreatePresentSampleView(rtW.PresentTarget);
					// AA intermediate: same sRGB-store + UNORM-sample pair (FXAA renders present <- intermediate).
					// Always allocated (one extra RGBA8 target/viewport, negligible); the FXAA pass only uses it
					// when render.aa != 0.
					rtW.AAIntermediateTarget = CreatePresentTarget(w, h, "ViewportAA");
					rtW.AAIntermediateSampleView = CreatePresentSampleView(rtW.AAIntermediateTarget);
					// Full-res HDR upscale target: the UpscalePass writes it from the low-res Target; tonemap
					// reads it when scale < 1. Same format as the scene Target so tonemap's bindless Load matches.
					rtW.SceneUpscaleTarget = CreateDefaultSceneRenderTarget(w, h, "ViewportUpscale");
				}
			}
		}
	}
}