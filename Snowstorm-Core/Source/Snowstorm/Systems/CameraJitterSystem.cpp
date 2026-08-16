#include "CameraJitterSystem.hpp"

#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Math/HaltonJitter.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void CameraJitterSystem::Execute(Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();

		// Jitter is on when explicitly enabled (render.jitter) OR when TAA is selected (render.aa == 2):
		// TAA without jitter accumulates identical frames — pure lag, no anti-aliasing — so selecting TAA
		// implies jitter, the way Unreal couples TemporalAA to its view jitter.
		//
		// TAA-in-compare (#98): with render.scale < 1, TAA is a temporal UPSCALER (accumulates jittered
		// sub-pixel samples at output res — the DLSS/XeSS substrate). Measuring whether that recovers the
		// detail bilinear can't needs the in-engine A/B (compare mode) WITH jitter — but compare normally
		// forces jitter OFF. That's correct for a purely SPATIAL upscaler (#99/#102: it can't win against a
		// per-frame shift, so a jittered A/B would just shimmer both sides), but wrong for TAAU. So when TAA
		// is the active AA mode, jitter stays on even in compare: only the LR forward consumes the jittered
		// VP (addForward jittered=true), while the GT forward always renders UNJITTERED (a clean full-res
		// reference) and velocity keeps the unjittered VP — so the metric stays valid, GT vs a TAA-resolved
		// LR that should converge toward it. Spatial-upscaler A/Bs (TAA off, render.aa != 2) are unchanged:
		// jitter still forced off in compare.
		//
		// EXCEPTION — dataset export (#46/#102): a TEMPORAL super-resolution network trains on JITTERED low-res
		// input (the sub-pixel offset is the only source of new detail it reconstructs). Export runs inside
		// compare mode (it needs the ground-truth render). So export forces jitter back on — but ONLY when
		// dataset.jitter is set (a spatial net trains/infers unjittered; that mismatch is what made the
		// trained spatial net lose to bilinear in-engine, #102). JitterNdc is recorded per frame.
		// DLAA (render.aa == 3) is a temporal resolve too — it accumulates sub-pixel jitter exactly like TAA,
		// so treat it the same for the jitter gate (and the negative MipBias that rides the jittered pass).
		const bool taaActive = CVars::AAMode.Get() == 2 || CVars::DlaaActive();
		const bool jitterOn = (CVars::DatasetExport.Get() && CVars::DatasetJitter.Get()) ||
		                      taaActive ||
		                      (CVars::Jitter.Get() && !CVars::Compare.Get());

		// Same monotonic counter the whole frame uses (incremented in RendererService::NewFrame before any
		// system runs). Deterministic per frame, so all cameras this frame share one Halton index.
		const uint64_t frame = ServiceView<RendererService>().GetFrameCounter();
		// 16-phase Halton ring (was 8): more sub-pixel samples so thin/sub-pixel edges (railings, wires) are
		// covered more often and accumulate instead of shimmering. UE uses 8 for TAA, more for TSR-class detail;
		// 16 is the cheap step for thin-feature convergence. Longer ring = slightly slower to fully converge.
		const glm::vec2 jitterPx = HaltonJitterPixels(frame, 16); // [-0.5, 0.5] px, or unused when off

		for (const auto camView = reg.view<CameraRuntimeComponent, CameraTargetComponent>(); const auto e : camView)
		{
			auto& rt = reg.get<CameraRuntimeComponent>(e); // untracked: jitter walks every frame, don't mark Changed

			if (!jitterOn)
			{
				rt.JitteredViewProjection = rt.ViewProjection; // clean no-op: color pass == unjittered
				rt.JitterNdc = glm::vec2(0.0f);
				continue;
			}

			// Convert the sub-pixel offset to NDC using the camera's TARGET render resolution (the scene
			// Target, already sized at render.scale) — so the same pixel offset is a correct sub-pixel shift
			// whether we render at native or internal resolution. NDC spans 2 units across `dim` pixels.
			float renderW = 0.0f;
			float renderH = 0.0f;
			if (const auto& ct = reg.get<CameraTargetComponent>(e); ct.TargetViewportEntity != entt::null &&
			                                                        reg.any_of<RenderTargetComponent>(ct.TargetViewportEntity))
			{
				if (const auto& vpRT = reg.Read<RenderTargetComponent>(ct.TargetViewportEntity); vpRT.Target)
				{
					renderW = static_cast<float>(vpRT.Target->GetWidth());
					renderH = static_cast<float>(vpRT.Target->GetHeight());
				}
			}

			if (renderW < 1.0f || renderH < 1.0f)
			{
				// No resolved target yet (first frames / mid-resize): skip jitter this frame, stay unjittered.
				rt.JitteredViewProjection = rt.ViewProjection;
				rt.JitterNdc = glm::vec2(0.0f);
				continue;
			}

			// Pixels -> clip/NDC offset (2 NDC units across the full width/height).
			const glm::vec2 jitterNdc{jitterPx.x * 2.0f / renderW, jitterPx.y * 2.0f / renderH};

			// Offset a COPY of the projection: for a column-major glm projection, adding to P[2][0]/P[2][1]
			// (the z-column x/y rows) shifts clip.xy proportionally to w, i.e. a constant sub-pixel shift in
			// NDC after the perspective divide — the standard TAA jitter injection. The canonical
			// ViewProjection + frustum are left untouched (motion vectors + culling read those).
			glm::mat4 jitteredProj = rt.Projection;
			jitteredProj[2][0] += jitterNdc.x;
			jitteredProj[2][1] += jitterNdc.y;

			rt.JitteredViewProjection = jitteredProj * rt.View;
			rt.JitterNdc = jitterNdc;
		}
	}
}
