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
				// GI target renders at render.gi.scale (#124), independent of render.scale.
				const uint32_t giW = ScaledExtent(w, CVars::ClampedGIScale());
				const uint32_t giH = ScaledExtent(h, CVars::ClampedGIScale());
				// AO target renders at render.ao.scale (#126), independent of both.
				const uint32_t aoW = ScaledExtent(w, CVars::ClampedAOScale());
				const uint32_t aoH = ScaledExtent(h, CVars::ClampedAOScale());

				const auto& rt = reg.Read<RenderTargetComponent>(vpEntity);
				const bool missing = !rt.Target || !rt.PresentTarget || !rt.AAIntermediateTarget || !rt.SceneUpscaleTarget ||
				                     !rt.GroundTruthTarget || !rt.GroundTruthPresentTarget || !rt.VelocityTarget ||
				                     !rt.GBufferNormalTarget || !rt.GITarget || !rt.GIHistory[0] || !rt.GIHistory[1] ||
				                     !rt.GIMoments[0] || !rt.GIMoments[1] || !rt.ReflMoments[0] || !rt.ReflMoments[1] ||
				                     !rt.GIDenoiseScratch[0] || !rt.GIDenoiseScratch[1] || !rt.GIUpscaleTarget ||
				                     !rt.AOTarget || !rt.AOUpscaleTarget ||
				                     !rt.ReflectionTarget || !rt.ReflHistory[0] || !rt.ReflHistory[1] ||
				                     !rt.ReflDenoiseScratch[0] || !rt.ReflDenoiseScratch[1] ||
				                     !rt.HistoryTarget[0] || !rt.HistoryTarget[1];
				// Present target tracks the FULL viewport size; Target tracks the SCALED size. Compare each
				// against its own expected extent so a scale change (Target only) still triggers a rebuild.
				const bool viewportResized = rt.PresentTarget && (rt.PresentTarget->GetDesc().Width != w || rt.PresentTarget->GetDesc().Height != h);
				const bool scaleChanged = rt.Target && (rt.Target->GetDesc().Width != sw || rt.Target->GetDesc().Height != sh);
				// GI/AO scales can change independently — rebuild each when its own scaled extent changes.
				const bool giScaleChanged = rt.GITarget && (rt.GITarget->GetDesc().Width != giW || rt.GITarget->GetDesc().Height != giH);
				const bool aoScaleChanged = rt.AOTarget && (rt.AOTarget->GetDesc().Width != aoW || rt.AOTarget->GetDesc().Height != aoH);
				if (missing || viewportResized || scaleChanged || giScaleChanged || aoScaleChanged)
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
					rtW.SceneUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportUpscale");
					// Ground-truth targets (compare mode, #43 part 2): full-res HDR scene + its LDR present pair.
					// Always allocated (negligible); only rendered into when render.compare is on.
					rtW.GroundTruthTarget = CreateDefaultSceneRenderTarget(w, h, "ViewportGT");
					rtW.GroundTruthPresentTarget = CreatePresentTarget(w, h, "ViewportGT");
					rtW.GroundTruthPresentSampleView = CreatePresentSampleView(rtW.GroundTruthPresentTarget);
					// Motion-vector target (#44): full viewport res (its own depth) so the tonemap debug view
					// reads it 1:1 via integer Load(). Always allocated (negligible); only rendered when
					// render.debugview != 0.
					rtW.VelocityTarget = CreateVelocityTarget(w, h, "Viewport");
					// Depth+normal G-buffer (#124): full viewport res (the bilateral upsample guide must be
					// full-res). Always allocated (negligible); only rendered when GI is active or the normal
					// debug view is selected.
					rtW.GBufferNormalTarget = CreateDepthNormalTarget(w, h, "Viewport");
					// Half-res GI target (#124): viewport * render.gi.scale. Always allocated (negligible); only
					// dispatched into when GI is active. Rebuilt on viewport OR gi.scale change.
					rtW.GITarget = CreateGITarget(giW, giH, "Viewport");
					rtW.GITargetView = rtW.GITarget->GetDefaultView();
					// GI temporal history ping-pong (#125): same half-res shape as GITarget. Always allocated;
					// only written when temporal accumulation runs. Rebuilt on viewport OR gi.scale change.
					rtW.GIHistory[0] = CreateGITarget(giW, giH, "ViewportGIHistory0");
					rtW.GIHistoryView[0] = rtW.GIHistory[0]->GetDefaultView();
					rtW.GIHistory[1] = CreateGITarget(giW, giH, "ViewportGIHistory1");
					rtW.GIHistoryView[1] = rtW.GIHistory[1]->GetDefaultView();
					rtW.GIMoments[0] = CreateGITarget(giW, giH, "ViewportGIMoments0"); // SVGF moments (#129 Inc 3c)
					rtW.GIMomentsView[0] = rtW.GIMoments[0]->GetDefaultView();
					rtW.GIMoments[1] = CreateGITarget(giW, giH, "ViewportGIMoments1");
					rtW.GIMomentsView[1] = rtW.GIMoments[1]->GetDefaultView();
					// GI denoiser ping-pong scratch pair (#125): same half-res shape as GITarget. Always allocated;
					// only written when the denoiser runs. Rebuilt on viewport OR gi.scale change (tracks GITarget).
					rtW.GIDenoiseScratch[0] = CreateGITarget(giW, giH, "ViewportGIDenoise0");
					rtW.GIDenoiseScratchView[0] = rtW.GIDenoiseScratch[0]->GetDefaultView();
					rtW.GIDenoiseScratch[1] = CreateGITarget(giW, giH, "ViewportGIDenoise1");
					rtW.GIDenoiseScratchView[1] = rtW.GIDenoiseScratch[1]->GetDefaultView();
					// Full-res GI target (#124): the bilateral upsample renders the half-res GI into this, and the
					// forward pass samples it (by screen UV) as the diffuse GI. Full viewport res.
					rtW.GIUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportGIUpscale");
					// Half-res AO target (#126): viewport * render.ao.scale. Always allocated; only dispatched
					// when AO is active. Rebuilt on viewport OR ao.scale change. Independent of GI.
					rtW.AOTarget = CreateAOTarget(aoW, aoH, "Viewport");
					rtW.AOTargetView = rtW.AOTarget->GetDefaultView();
					// Full-res AO target (#126): the bilateral upsample renders the half-res AO into this; the
					// forward pass samples it (by screen UV) and folds it into `ao`. Full viewport res.
					rtW.AOUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportAOUpscale");
					// Full-res RT reflection + temporal history (#129): full viewport res (reflections are
					// high-frequency). Always allocated; only dispatched when reflections are active. Rebuilt on
					// viewport resize (not gi.scale — these are full-res).
					rtW.ReflectionTarget = CreateGITarget(w, h, "ViewportReflection");
					rtW.ReflectionTargetView = rtW.ReflectionTarget->GetDefaultView();
					rtW.ReflHistory[0] = CreateGITarget(w, h, "ViewportReflHistory0");
					rtW.ReflHistoryView[0] = rtW.ReflHistory[0]->GetDefaultView();
					rtW.ReflHistory[1] = CreateGITarget(w, h, "ViewportReflHistory1");
					rtW.ReflHistoryView[1] = rtW.ReflHistory[1]->GetDefaultView();
					rtW.ReflMoments[0] = CreateGITarget(w, h, "ViewportReflMoments0"); // SVGF moments (#129 Inc 3c)
					rtW.ReflMomentsView[0] = rtW.ReflMoments[0]->GetDefaultView();
					rtW.ReflMoments[1] = CreateGITarget(w, h, "ViewportReflMoments1");
					rtW.ReflMomentsView[1] = rtW.ReflMoments[1]->GetDefaultView();
					rtW.ReflDenoiseScratch[0] = CreateGITarget(w, h, "ViewportReflDenoise0");
					rtW.ReflDenoiseScratchView[0] = rtW.ReflDenoiseScratch[0]->GetDefaultView();
					rtW.ReflDenoiseScratch[1] = CreateGITarget(w, h, "ViewportReflDenoise1");
					rtW.ReflDenoiseScratchView[1] = rtW.ReflDenoiseScratch[1]->GetDefaultView();
					// TAA history ping-pong (#44): two full-res color-only HDR targets. Always allocated;
					// only rendered into when render.aa == TAA. Recreated on resize so history matches size.
					rtW.HistoryTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory0");
					rtW.HistoryTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory1");
				}
			}
		}
	}
}