#include "ViewportResizeSystem.hpp"

#include "Service/ImGuiService.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Systems/ViewportResizeRule.hpp"

namespace Snowstorm
{
	namespace
	{
		using ViewportResizeRule::Extent;

		template <typename T>
		Extent ExtentOf(const Ref<T>& t)
		{
			return {t->GetDesc().Width, t->GetDesc().Height};
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

		// An internal-scale CVar (render.scale / render.gi.scale / render.ao.scale) is GLOBAL state, not a
		// viewport-size change, so the size-change gates below would skip the rebuild and the sliders would
		// silently no-op. Detect a change once per frame and treat it like a viewport change so the per-scale
		// target rebuild (scaleChanged/giScaleChanged/aoScaleChanged) is actually reached.
		const float curRenderScale = CVars::ClampedRenderScale();
		const float curGIScale = CVars::ClampedGIScale();
		const float curAOScale = CVars::ClampedAOScale();
		// Forward MSAA (render.msaa) is also a global CVar edit; a change forces the same rebuild path so it
		// applies live (scene targets reallocated at the new sample count + material/sky pipelines rebuilt).
		const uint32_t curMsaa = CVars::MsaaSampleCount();
		const bool scaleChangedGlobal = curRenderScale != m_LastRenderScale ||
		                                curGIScale != m_LastGIScale ||
		                                curAOScale != m_LastAOScale ||
		                                curMsaa != m_LastMsaa;
		m_LastRenderScale = curRenderScale;
		m_LastGIScale = curGIScale;
		m_LastAOScale = curAOScale;
		m_LastMsaa = curMsaa;

		// If you want: when no viewport changed, you can still init new cameras and early-out.
		const bool anyViewportChanged = !changedViewports.empty();
		if (!ViewportResizeRule::ShouldRun(anyViewportChanged, !cameraInit.empty(), scaleChangedGlobal))
		{
			return;
		}

		for (const entt::entity vpEntity : viewportView)
		{
			// If ImGui is enabled, we resize only when the viewport size changed (or an internal scale changed,
			// or a camera is initializing). If ImGui is disabled, the viewport matches the window each frame.
			if (!ViewportResizeRule::ShouldVisit(isImGuiEnabled, changedViewports.contains(vpEntity), !cameraInit.empty(), scaleChangedGlobal))
			{
				continue;
			}

			const auto& vp = reg.Read<ViewportComponent>(vpEntity);

			const Extent panel{static_cast<uint32_t>(vp.Size.x), static_cast<uint32_t>(vp.Size.y)};
			const Extent window = isImGuiEnabled ? Extent{} : Extent{app.GetWindow().GetWidth(), app.GetWindow().GetHeight()};
			const auto resolved = ViewportResizeRule::ResolveSize(isImGuiEnabled, panel, window, CVars::ResolutionWidth.Get(), CVars::ResolutionHeight.Get());
			if (!resolved.Valid)
			{
				continue;
			}
			const uint32_t w = resolved.Size.W;
			const uint32_t h = resolved.Size.H;

			if (!isImGuiEnabled)
			{
				// Non-imgui path: viewport = window size (or the forced resolution); mark viewport as changed
				auto& vpW = reg.Write<ViewportComponent>(vpEntity);
				vpW.Size = {static_cast<float>(w), static_cast<float>(h)};
			}

			// Rebuild the scene + present + AA + upscale targets together, when missing, viewport-resized,
			// OR the internal render scale changed (which resizes only the low-res scene Target).
			{
				// Scene Target renders at render.scale (#43); GI (#124), AO (#126) and sun shadow have their own
				// independent scales; everything else stays full viewport res.
				const auto want = ViewportResizeRule::ScaledExtents(
				    resolved.Size, {CVars::ClampedRenderScale(), CVars::ClampedGIScale(), CVars::ClampedAOScale(), CVars::ClampedShadowScale()});
				const uint32_t sw = want.Scene.W, sh = want.Scene.H;
				const uint32_t giW = want.GI.W, giH = want.GI.H;
				const uint32_t aoW = want.AO.W, aoH = want.AO.H;
				const uint32_t shadowW = want.Shadow.W, shadowH = want.Shadow.H;

				const auto& rt = reg.Read<RenderTargetComponent>(vpEntity);
				const bool missing = !rt.Target || !rt.PresentTarget || !rt.AAIntermediateTarget || !rt.SceneUpscaleTarget ||
				                     !rt.GroundTruthTarget || !rt.GroundTruthPresentTarget || !rt.VelocityTarget ||
				                     !rt.GBufferNormalTarget || !rt.GITarget || !rt.GIDenoiser.Allocated() || !rt.GIUpscaleTarget[0] || !rt.GIUpscaleTarget[1] ||
				                     !rt.AOTarget || !rt.AOBlurTarget || !rt.AODenoiser.Allocated() || !rt.AOUpscaleTarget[0] || !rt.AOUpscaleTarget[1] ||
				                     !rt.ShadowTarget || !rt.ShadowDenoiser.Allocated() || !rt.ShadowUpscaleTarget[0] || !rt.ShadowUpscaleTarget[1] ||
				                     !rt.ShadowSpecTarget || !rt.ShadowSpecDenoiser.Allocated() || !rt.ShadowSpecUpscaleTarget[0] || !rt.ShadowSpecUpscaleTarget[1] ||
				                     !rt.ReflectionTarget || !rt.ReflectionDenoiser.Allocated() || !rt.PrevSceneColorTarget ||
				                     !rt.PathTraceAccumTarget ||
				                     !rt.HistoryTarget[0] || !rt.HistoryTarget[1];
				// Present target tracks the FULL viewport size; Target tracks the SCALED size. Each is compared
				// against its own expected extent so a scale change (Target only) still triggers a rebuild. Live
				// MSAA: the scene target's color-attachment sample count records the active level.
				ViewportResizeRule::Current cur;
				cur.Missing = missing;
				if (rt.PresentTarget)
				{
					cur.HasPresent = true;
					cur.Present = ExtentOf(rt.PresentTarget);
				}
				if (rt.Target)
				{
					cur.HasScene = true;
					cur.Scene = ExtentOf(rt.Target);
				}
				if (rt.GITarget)
				{
					cur.HasGI = true;
					cur.GI = ExtentOf(rt.GITarget);
				}
				if (rt.AOTarget)
				{
					cur.HasAO = true;
					cur.AO = ExtentOf(rt.AOTarget);
				}
				if (rt.ShadowTarget)
				{
					cur.HasShadow = true;
					cur.Shadow = ExtentOf(rt.ShadowTarget);
				}
				if (rt.Target && !rt.Target->GetDesc().ColorAttachments.empty())
				{
					cur.HasMsaaSource = true;
					cur.Msaa = rt.Target->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().SampleCount;
				}
				const bool msaaChanged = cur.HasMsaaSource && cur.Msaa != curMsaa;
				if (ViewportResizeRule::NeedsRebuild(cur, resolved.Size, want, curMsaa))
				{
					// Dropping the old targets is safe mid-flight: texture images and views retire through the
					// deferred-deletion queue, and bindless slots through their own quarantine.
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
					// GI SVGF denoiser buffers (#132): half-res, rebuilt on viewport OR gi.scale change (tracks GITarget).
					AllocateDenoiser(rtW.GIDenoiser, giW, giH, "ViewportGI");
					AllocateReservoir(rtW.GIReservoir, giW, giH, "ViewportGI");
					// Full-res GI target (#124): the bilateral upsample renders the half-res GI into this, and the
					// forward pass samples it (by screen UV) as the diffuse GI. Full viewport res.
					rtW.GIUpscaleTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportGIUpscale0");
					rtW.GIUpscaleTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportGIUpscale1");
					// Half-res AO target (#126): viewport * render.ao.scale. Always allocated; only dispatched
					// when AO is active. Rebuilt on viewport OR ao.scale change. Independent of GI.
					rtW.AOTarget = CreateAOTarget(aoW, aoH, "Viewport");
					rtW.AOTargetView = rtW.AOTarget->GetDefaultView();
					// SSAO blur output (#151): same half-res shape as AOTarget; the SSAO bilateral blur writes it and
					// the shared upsample reads it. Always allocated (negligible); only written when SSAO is active.
					rtW.AOBlurTarget = CreateAOTarget(aoW, aoH, "ViewportAOBlur");
					rtW.AOBlurTargetView = rtW.AOBlurTarget->GetDefaultView();
					// AO SVGF denoiser buffers (#130): half-res, rebuilt on viewport OR ao.scale change (tracks AOTarget).
					AllocateDenoiser(rtW.AODenoiser, aoW, aoH, "ViewportAO");
					// Full-res AO target (#126): the bilateral upsample renders the half-res AO into this; the
					// forward pass samples it (by screen UV) and folds it into `ao`. Full viewport res.
					rtW.AOUpscaleTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportAOUpscale0");
					rtW.AOUpscaleTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportAOUpscale1");
					// Half-res sun-shadow target: render.shadows.scale (shadowW/shadowH), rebuilt on viewport OR
					// shadows.scale change (tracked by shadowScaleChanged). Only dispatched when ShadowsRTActive().
					rtW.ShadowTarget = CreateAOTarget(shadowW, shadowH, "ViewportShadow");
					rtW.ShadowTargetView = rtW.ShadowTarget->GetDefaultView();
					// Stochastic shadow SVGF denoiser buffers: half-res, rebuilt on viewport OR shadows.scale change
					// (tracks ShadowTarget). Required — 1 ray/pixel is very noisy without temporal+à-trous.
					AllocateDenoiser(rtW.ShadowDenoiser, shadowW, shadowH, "ViewportShadow");
					// Full-res sun-shadow target: the bilateral upsample renders the half-res shadow (after temporal+
					// denoise) into this; the forward pass samples it (by screen UV) as the aggregate ratio. Full res.
					rtW.ShadowUpscaleTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportShadowUpscale0");
					rtW.ShadowUpscaleTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportShadowUpscale1");
					// Specular twin of the shadow chain (demodulated MegaLights specular): same half-res grid + its own
					// denoiser, so the specular signal denoises independently of the diffuse. Full-res upscale target.
					rtW.ShadowSpecTarget = CreateAOTarget(shadowW, shadowH, "ViewportShadowSpec");
					rtW.ShadowSpecTargetView = rtW.ShadowSpecTarget->GetDefaultView();
					AllocateDenoiser(rtW.ShadowSpecDenoiser, shadowW, shadowH, "ViewportShadowSpec");
					rtW.ShadowSpecUpscaleTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportShadowSpecUpscale0");
					rtW.ShadowSpecUpscaleTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportShadowSpecUpscale1");
					// Full-res RT reflection (#129): full viewport res (reflections are high-frequency). Always
					// allocated; only dispatched when reflections are active. Rebuilt on viewport resize.
					rtW.ReflectionTarget = CreateGITarget(w, h, "ViewportReflection");
					rtW.ReflectionTargetView = rtW.ReflectionTarget->GetDefaultView();
					// Reflection SVGF denoiser buffers (#132): full-res, rebuilt on viewport resize.
					AllocateDenoiser(rtW.ReflectionDenoiser, w, h, "ViewportRefl");
					// Previous-frame HDR scene color (#151, SSR): full-res, always allocated (negligible); only
					// snapshotted + read when SSR is active. Rebuilt on viewport resize like the history targets.
					rtW.PrevSceneColorTarget = CreateColorOnlyHDRTarget(w, h, "ViewportPrevColor");
					// Path-tracer accumulation (#153): full-res fp32, always allocated (only written in PT mode).
					rtW.PathTraceAccumTarget = CreatePathTraceTarget(w, h, "Viewport");
					rtW.PathTraceAccumView = rtW.PathTraceAccumTarget->GetDefaultView();
					// TAA history ping-pong (#44): two full-res color-only HDR targets. Always allocated;
					// only rendered into when render.aa == TAA. Recreated on resize so history matches size.
					rtW.HistoryTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory0");
					rtW.HistoryTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory1");

					// The scene targets above were (re)built at curMsaa; the scene material pipelines must match
					// or the forward pass would mix a 4x target with 1x pipelines (validation error / no draw).
					// Rebuild the cached material pipelines in place now — the GPU was drained above, and material
					// instances share the same Pipeline object, so the swap reaches them with no re-resolution.
					// No-op on a plain resize (sample count unchanged). The sky pipeline self-heals in SkyPass.
					m_World->GetSingleton<AssetManagerSingleton>().RebuildPipelinesForSampleCount(curMsaa);

					if (msaaChanged || (missing && curMsaa != 1))
					{
						SS_CORE_INFO("Forward MSAA: {}x", curMsaa);
					}
				}
			}
		}
	}
}