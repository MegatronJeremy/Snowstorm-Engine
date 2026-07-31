#include "RuntimeInitSystem.hpp"

#include <unordered_map>

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

// Persistent-ish / serialized
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"

// Runtime-only
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraControllerRuntimeComponent.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"

namespace Snowstorm
{
	namespace
	{
		bool IsValidViewportSize(uint32_t w, uint32_t h)
		{
			// Match your earlier guardrails (avoid tiny/zero RTs)
			return (w >= 64u && h >= 64u);
		}
	}

	void RuntimeInitSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();

		// ---------------------------------------------------------------------
		// Build UUID -> entt::entity map (used for resolving entity references)
		// ---------------------------------------------------------------------
		std::unordered_map<UUID::UnderlyingType, entt::entity> uuidToEntity;

		for (const entt::entity e : reg.view<IDComponent>())
		{
			const auto& id = reg.Read<IDComponent>(e).Id;
			if (id.Value() != 0)
			{
				uuidToEntity[id.Value()] = e;
			}
		}

		// ---------------------------------------------------------------------
		// Ensure runtime/editor-only viewport interaction exists
		// ---------------------------------------------------------------------
		for (const entt::entity e : reg.view<ViewportComponent>())
		{
			reg.Ensure<ViewportInteractionComponent>(e);
		}

		// ---------------------------------------------------------------------
		// Resolve CameraTargetComponent's runtime cache: TargetViewportEntity
		// ---------------------------------------------------------------------
		for (const entt::entity e : reg.view<CameraTargetComponent>())
		{
			const auto& ct = reg.Read<CameraTargetComponent>(e);

			entt::entity resolved = entt::null;
			if (ct.TargetViewportUUID.Value() != 0)
			{
				if (auto it = uuidToEntity.find(ct.TargetViewportUUID.Value()); it != uuidToEntity.end())
					resolved = it->second;
			}

			if (resolved != ct.TargetViewportEntity)
			{
				auto& w = reg.Write<CameraTargetComponent>(e);
				w.TargetViewportEntity = resolved;
			}
		}

		// ---------------------------------------------------------------------
		// Ensure RenderTargetComponent exists on viewport entities, and (re)build GPU RT when needed
		//
		// IMPORTANT:
		// - Ensure keeps the component and does NOT mark Changed every frame.
		// - Only rebuild RT (and thus Write) when missing or size mismatch.
		// ---------------------------------------------------------------------
		for (const entt::entity e : reg.view<ViewportComponent>())
		{
			const auto& vp = reg.Read<ViewportComponent>(e);

			const uint32_t w = static_cast<uint32_t>(vp.Size.x);
			const uint32_t h = static_cast<uint32_t>(vp.Size.y);

			if (!IsValidViewportSize(w, h))
				continue;

			auto& rtc = reg.Ensure<RenderTargetComponent>(e);

			// Scene Target renders at render.scale (#43); present/AA/upscale stay full viewport res.
			const float scale = CVars::ClampedRenderScale();
			const uint32_t sw = ScaledExtent(w, scale);
			const uint32_t sh = ScaledExtent(h, scale);

			bool needsCreate = false;

			const uint32_t giW = ScaledExtent(w, CVars::ClampedGIScale());
			const uint32_t giH = ScaledExtent(h, CVars::ClampedGIScale());
			const uint32_t aoW = ScaledExtent(w, CVars::ClampedAOScale());
			const uint32_t aoH = ScaledExtent(h, CVars::ClampedAOScale());

			if (!rtc.Target || !rtc.PresentTarget || !rtc.AAIntermediateTarget || !rtc.SceneUpscaleTarget ||
			    !rtc.GroundTruthTarget || !rtc.GroundTruthPresentTarget || !rtc.VelocityTarget ||
			    !rtc.GBufferNormalTarget || !rtc.GITarget || !rtc.GIUpscaleTarget ||
			    !rtc.AOTarget || !rtc.AOUpscaleTarget ||
			    !rtc.HistoryTarget[0] || !rtc.HistoryTarget[1])
			{
				needsCreate = true;
			}
			else
			{
				// Present tracks full size; Target tracks scaled size; GI/AO track their own scaled — check each
				// so any scale change rebuilds.
				const auto& presentDesc = rtc.PresentTarget->GetDesc();
				const auto& targetDesc = rtc.Target->GetDesc();
				if (presentDesc.Width != w || presentDesc.Height != h || targetDesc.Width != sw || targetDesc.Height != sh ||
				    rtc.GITarget->GetDesc().Width != giW || rtc.GITarget->GetDesc().Height != giH ||
				    rtc.AOTarget->GetDesc().Width != aoW || rtc.AOTarget->GetDesc().Height != aoH)
				{
					needsCreate = true;
				}
			}

			if (needsCreate)
			{
				auto& wRtc = reg.Write<RenderTargetComponent>(e);
				wRtc.Target = CreateDefaultSceneRenderTarget(sw, sh, "Viewport");
				wRtc.PresentTarget = CreatePresentTarget(w, h, "Viewport");
				wRtc.PresentSampleView = CreatePresentSampleView(wRtc.PresentTarget);
				wRtc.AAIntermediateTarget = CreatePresentTarget(w, h, "ViewportAA");
				wRtc.AAIntermediateSampleView = CreatePresentSampleView(wRtc.AAIntermediateTarget);
				wRtc.SceneUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportUpscale");
				wRtc.GroundTruthTarget = CreateDefaultSceneRenderTarget(w, h, "ViewportGT");
				wRtc.GroundTruthPresentTarget = CreatePresentTarget(w, h, "ViewportGT");
				wRtc.GroundTruthPresentSampleView = CreatePresentSampleView(wRtc.GroundTruthPresentTarget);
				wRtc.VelocityTarget = CreateVelocityTarget(w, h, "Viewport");         // motion vectors (#44), full viewport res
				wRtc.GBufferNormalTarget = CreateDepthNormalTarget(w, h, "Viewport"); // depth+normal G-buffer (#124), full res
				wRtc.GITarget = CreateGITarget(giW, giH, "Viewport");                 // half-res GI (#124)
				wRtc.GITargetView = wRtc.GITarget->GetDefaultView();
				wRtc.GIHistory[0] = CreateGITarget(giW, giH, "ViewportGIHistory0"); // GI temporal history (#125)
				wRtc.GIHistoryView[0] = wRtc.GIHistory[0]->GetDefaultView();
				wRtc.GIHistory[1] = CreateGITarget(giW, giH, "ViewportGIHistory1");
				wRtc.GIHistoryView[1] = wRtc.GIHistory[1]->GetDefaultView();
				wRtc.GIDenoiseScratch[0] = CreateGITarget(giW, giH, "ViewportGIDenoise0"); // denoiser ping-pong (#125)
				wRtc.GIDenoiseScratchView[0] = wRtc.GIDenoiseScratch[0]->GetDefaultView();
				wRtc.GIDenoiseScratch[1] = CreateGITarget(giW, giH, "ViewportGIDenoise1");
				wRtc.GIDenoiseScratchView[1] = wRtc.GIDenoiseScratch[1]->GetDefaultView();
				wRtc.GIUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportGIUpscale"); // full-res GI (#124)
				wRtc.AOTarget = CreateAOTarget(aoW, aoH, "Viewport");                       // half-res AO (#126)
				wRtc.AOTargetView = wRtc.AOTarget->GetDefaultView();
				wRtc.AOUpscaleTarget = CreateColorOnlyHDRTarget(w, h, "ViewportAOUpscale"); // full-res AO (#126)
				wRtc.ReflectionTarget = CreateGITarget(w, h, "ViewportReflection");         // full-res RT reflection (#129)
				wRtc.ReflectionTargetView = wRtc.ReflectionTarget->GetDefaultView();
				wRtc.ReflHistory[0] = CreateGITarget(w, h, "ViewportReflHistory0");         // reflection temporal history (#129)
				wRtc.ReflHistoryView[0] = wRtc.ReflHistory[0]->GetDefaultView();
				wRtc.ReflHistory[1] = CreateGITarget(w, h, "ViewportReflHistory1");
				wRtc.ReflHistoryView[1] = wRtc.ReflHistory[1]->GetDefaultView();
				wRtc.ReflDenoiseScratch[0] = CreateGITarget(w, h, "ViewportReflDenoise0"); // reflection à-trous ping-pong (#129 Inc 3a)
				wRtc.ReflDenoiseScratchView[0] = wRtc.ReflDenoiseScratch[0]->GetDefaultView();
				wRtc.ReflDenoiseScratch[1] = CreateGITarget(w, h, "ViewportReflDenoise1");
				wRtc.ReflDenoiseScratchView[1] = wRtc.ReflDenoiseScratch[1]->GetDefaultView();
				wRtc.HistoryTarget[0] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory0"); // TAA history (#44)
				wRtc.HistoryTarget[1] = CreateColorOnlyHDRTarget(w, h, "ViewportHistory1");
			}
		}

		// ---------------------------------------------------------------------
		// Ensure camera runtime cache exists (RenderSystem should rely on this)
		// ---------------------------------------------------------------------
		for (const entt::entity e : reg.view<CameraComponent, TransformComponent>())
		{
			reg.Ensure<CameraRuntimeComponent>(e);
		}

		// ---------------------------------------------------------------------
		// Ensure controller runtime exists for controller-driven cameras
		// (this is where you keep "last mouse pos", "was RMB held", etc)
		// ---------------------------------------------------------------------
		for (const entt::entity e : reg.view<CameraControllerComponent, CameraComponent, TransformComponent>())
		{
			reg.Ensure<CameraControllerRuntimeComponent>(e);
		}
	}
}
