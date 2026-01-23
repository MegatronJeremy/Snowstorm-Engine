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

			bool needsCreate = false;

			if (!rtc.Target)
			{
				needsCreate = true;
			}
			else
			{
				const auto& desc = rtc.Target->GetDesc();
				if (desc.Width != w || desc.Height != h)
				{
					needsCreate = true;
				}
			}

			if (needsCreate)
			{
				auto& wRtc = reg.Write<RenderTargetComponent>(e);
				wRtc.Target = CreateDefaultSceneRenderTarget(w, h, "Viewport");
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
