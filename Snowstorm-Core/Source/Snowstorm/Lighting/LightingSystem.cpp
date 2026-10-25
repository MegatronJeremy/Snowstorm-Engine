#include "LightingSystem.hpp"

#include "LightingComponents.hpp"
#include "LightPacking.hpp"
#include "LightingUniforms.hpp"
#include "PrimaryDirectional.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Render/RendererService.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Snowstorm
{
	void LightingSystem::Execute(Timestep ts)
	{
		auto lightView = View<DirectionalLightComponent>();
		// A directional light that also carries a Rotator and a Transform is turned by that transform, so the
		// RotatorSystem can sweep a sun without a bespoke animation component. Others keep the authored Direction.
		const auto lightDirection = [this](const entt::entity entity, const DirectionalLightComponent& dl)
		{
			const auto& registry = m_World->GetRegistry();
			if (!registry.all_of<RotatorComponent, TransformComponent>(entity))
			{
				return dl.Direction;
			}
			const glm::mat3 rotation(registry.Read<TransformComponent>(entity).GetTransformMatrix());
			return RotatedLightDirection(rotation, dl.Direction);
		};
		auto& renderer3DSingleton = ServiceView<RendererService>();

		LightDataBlock lightData;
		bool droppedLights = false;
		// Primary sun = first ENABLED directional (DirectionalLights[0] in the shader); the shadow fit below
		// uses the same light.
		std::vector<DirectionalShadowCandidate> candidates;
		std::vector<glm::vec3> candidateDirs;
		for (auto entity : lightView)
		{
			const auto& dl = lightView.get<DirectionalLightComponent>(entity);
			candidates.push_back({dl.Enabled, dl.CastShadows});
			candidateDirs.push_back(lightDirection(entity, dl));
		}
		const size_t sunIndex = FindPrimaryDirectional(candidates);
		const bool haveSun = sunIndex < candidates.size();
		const glm::vec3 sunDir = haveSun ? candidateDirs[sunIndex] : glm::vec3{0.0f};
		const bool sunCasts = haveSun && candidates[sunIndex].CastShadows;
		const int directionalBudget = std::clamp(CVars::ShadowDirectionals.Get(), 1, MAX_DIRECTIONAL_LIGHTS);
		const DirectionalShadowSlots dirSlots = SelectShadowDirectionals(candidates, directionalBudget, MAX_SECONDARY_DIR_SHADOWS);
		const bool rasterDirShadows = CVars::ShadowsRasterActive();
		const bool rtDirShadows = CVars::ShadowsRTActive();
		size_t lightIndex = static_cast<size_t>(-1);
		for (auto entity : lightView)
		{
			++lightIndex;
			// LightDataBlock::Lights is a fixed GPUDirectionalLight[MAX_DIRECTIONAL_LIGHTS] mirrored on the
			// GPU; writing past it corrupts the adjacent LightCount/Padding and overruns the shader array.
			if (DirectionalFull(lightData))
			{
				droppedLights = true;
				break;
			}

			auto& directionalLight = lightView.get<DirectionalLightComponent>(entity);
			if (!directionalLight.Enabled) // per-light off: contributes no light and can't be the sun
			{
				continue;
			}
			// Raster: primary -> sun map (slot 0, fit above), secondaries -> a scene-fit tile of the directional
			// atlas. RT: every casting directional traces (the slot is only the "casts" sentinel).
			int shadowSlot = -1;
			if (rtDirShadows)
			{
				shadowSlot = directionalLight.CastShadows ? 0 : -1;
			}
			else if (rasterDirShadows)
			{
				shadowSlot = dirSlots.Slots[lightIndex];
				if (shadowSlot > 0)
				{
					GPUDirShadow& payload = lightData.DirShadows[shadowSlot - 1];
					if (ShadowPass::ComputeSunViewProj(*m_World, candidateDirs[lightIndex], payload.ViewProj))
					{
						constexpr float inv = 1.0f / static_cast<float>(ShadowPass::kDirAtlasCols);
						const int tile = shadowSlot - 1;
						const int col = tile % static_cast<int>(ShadowPass::kDirAtlasCols);
						const int row = tile / static_cast<int>(ShadowPass::kDirAtlasCols);
						payload.AtlasRect = {static_cast<float>(col) * inv, static_cast<float>(row) * inv, inv, inv};
					}
					else
					{
						shadowSlot = -1; // no scene bounds
					}
				}
			}
			lightData.Lights[lightData.LightCount++] =
			    PackDirectionalLight(candidateDirs[lightIndex], directionalLight.Intensity, directionalLight.Color, shadowSlot);
		}

		if (droppedLights && !m_WarnedDroppedDirectional)
		{
			SS_CORE_WARN("More than {} directional lights in scene; extra lights ignored.", MAX_DIRECTIONAL_LIGHTS);
		}
		m_WarnedDroppedDirectional = droppedLights;

		const bool droppedDirShadow = rasterDirShadows && dirSlots.Dropped > 0;
		if (droppedDirShadow && !m_WarnedDroppedDirShadow)
		{
			SS_CORE_WARN("{} shadow-casting directional light(s) over render.shadow.directionals = {}; they render "
			             "unshadowed in raster mode.",
			             dirSlots.Dropped, directionalBudget);
		}
		m_WarnedDroppedDirShadow = droppedDirShadow;

		// Directional-sun shadow FIT (world -> light clip), the sun analogue of the per-spot fit computed
		// below. Kept here so ALL shadow setup lives in the light system; RenderSystem only binds the depth
		// resource + records the pass from this result. Gated by the global render.shadows kill-switch AND
		// the sun's authored CastShadows flag; ComputeSunViewProj also fails (Valid stays false) when the
		// scene has no renderable bounds, in which case RenderSystem leaves ShadowMapIndex 0 (fully lit).
		RendererService::SunShadowFit sunFit{};
		if (CVars::ShadowsRasterActive() && haveSun && sunCasts)
		{
			sunFit.Valid = ShadowPass::ComputeSunViewProj(*m_World, sunDir, sunFit.LightViewProj);
		}
		renderer3DSingleton.SetSunShadowFit(sunFit);

		// Cascade fit (#58 PR 2): diagnostic handoff only, nothing renders from it yet. Uses the first Primary
		// perspective camera and its UNJITTERED view-projection so the fit does not shimmer with TAA jitter.
		RendererService::SunCascadeFit cascadeFit{};
		if (CVars::ShadowCascadeFit.Get() && haveSun && sunCasts && CVars::ShadowsRasterActive())
		{
			for (auto camView = View<CameraComponent, CameraRuntimeComponent>(); auto camEntity : camView)
			{
				const auto& cam = camView.get<CameraComponent>(camEntity);
				if (!cam.Primary || cam.Projection != CameraComponent::ProjectionType::Perspective)
				{
					continue;
				}
				const glm::mat4 invViewProj = glm::inverse(camView.get<CameraRuntimeComponent>(camEntity).ViewProjection);
				const uint32_t tile = static_cast<uint32_t>(std::max(CVars::ShadowResolution.Get(), 1));
				const float lambda = std::clamp(CVars::ShadowCascadeLambda.Get(), 0.0f, 1.0f);
				// Pullback: a fraction of the far distance so tall casters behind the slice still land in the map.
				cascadeFit.Fit = CascadeMath::ComputeFit(
				    invViewProj, cam.PerspectiveNear, cam.PerspectiveFar, sunDir,
				    static_cast<uint32_t>(std::clamp(CVars::ShadowCascadeCount.Get(), 1, 4)), lambda, tile,
				    cam.PerspectiveFar * 0.25f);
				cascadeFit.Valid = true;
				break;
			}
		}
		renderer3DSingleton.SetSunCascadeFit(cascadeFit);

		const bool shadowsEnabled = CVars::ShadowsRasterActive(); // raster atlas tiles (points + spots)
		// Under RT shadows (#118) there is no atlas budget: every casting light gets a ray-traced shadow. We
		// mark a caster with the >= 0 sentinel (ShadowSlot/ShadowIndex) the shader gate reads as "trace me",
		// WITHOUT consuming an atlas tile or filling the (unused-under-RT) matrices/rects.
		const bool rtShadows = CVars::ShadowsRTActive();

		// Point lights: position from the entity transform (Unity/Unreal model -- the light carries no
		// position of its own). Joined with TransformComponent so an untransformed light is simply skipped.
		int nextPointShadowSlot = 0; // next free point-shadow payload slot (6 atlas tiles each)
		bool droppedPoint = false;
		bool droppedPointShadow = false; // a casting point exceeded the shadow budget (renders unshadowed)
		for (auto pointView = View<PointLightComponent, TransformComponent>(); auto entity : pointView)
		{
			if (PointFull(lightData))
			{
				droppedPoint = true;
				break;
			}
			const auto& light = pointView.get<PointLightComponent>(entity);
			if (!light.Enabled) // per-light off: skip entirely (no light, no shadow slot)
			{
				continue;
			}
			const auto& transform = pointView.get<TransformComponent>(entity);

			// Assign a shadow payload slot if this omni casts, shadows are globally enabled, and a slot is
			// free (cap = MAX_SHADOW_POINTS -- each costs 6 depth passes). ShadowSlot < 0 => unshadowed. Fill
			// the slot's 6 cube-face view-projs + atlas rects (the cube unrolled: 6 consecutive tiles in the
			// kPointAtlasCols grid, tile index = slot*6 + face). The matrices/rects are pure math here;
			// RenderSystem renders each tile and binds the atlas texture (mirrors the spot path exactly).
			int shadowSlot = -1;
			if (rtShadows && light.CastShadows)
			{
				// RT: mark as casting (shader traces to the light); the payload slot is unused, so use 0 as a
				// sacrificial >= 0 sentinel. No atlas budget => every caster gets a shadow.
				shadowSlot = 0;
			}
			else if (shadowsEnabled && light.CastShadows)
			{
				if (nextPointShadowSlot < MAX_SHADOW_POINTS)
				{
					shadowSlot = nextPointShadowSlot++;
					GPUPointShadow& payload = lightData.PointShadows[shadowSlot];
					constexpr float inv = 1.0f / static_cast<float>(ShadowPass::kPointAtlasCols);
					for (int face = 0; face < 6; ++face)
					{
						const int tile = shadowSlot * 6 + face;
						const int col = tile % static_cast<int>(ShadowPass::kPointAtlasCols);
						const int row = tile / static_cast<int>(ShadowPass::kPointAtlasCols);
						payload.Face[face] = ShadowPass::ComputePointFaceViewProj(transform.Position, face, light.Range);
						payload.Rect[face] = {static_cast<float>(col) * inv, static_cast<float>(row) * inv, inv, inv};
					}
				}
				else
				{
					// More shadow-casting points than the atlas budget (each needs 6 tiles). The extra ones
					// render unshadowed rather than fighting over tiles -- fail loud so it's not a silent gap.
					droppedPointShadow = true;
				}
			}

			lightData.PointLights[lightData.PointCount++] =
			    PackPointLight(transform.Position, light.Range, light.Color, light.Intensity, shadowSlot);
		}
		lightData.PointShadowCount = nextPointShadowSlot;
		if (droppedPoint && !m_WarnedDroppedPoint)
		{
			SS_CORE_WARN("More than {} point lights in scene; extra lights ignored.", MAX_POINT_LIGHTS);
		}
		m_WarnedDroppedPoint = droppedPoint;
		if (droppedPointShadow && !m_WarnedDroppedPointShadow)
		{
			SS_CORE_WARN("More than {} shadow-casting point lights; extra ones render unshadowed (each omni "
			             "shadow costs 6 depth passes).",
			             MAX_SHADOW_POINTS);
		}
		m_WarnedDroppedPointShadow = droppedPointShadow;

		// Spot lights: position + forward (-Z) from the transform; cone half-angles stored as cosines so
		// the shader compares against dot() with no per-fragment trig. OuterAngle is clamped >= InnerAngle
		// so cos(inner) >= cos(outer) and the falloff denominator stays positive.
		int nextShadowTile = 0; // next free atlas tile for a shadow-casting spot
		bool droppedSpot = false;
		for (auto spotView = View<SpotLightComponent, TransformComponent>(); auto entity : spotView)
		{
			if (SpotFull(lightData))
			{
				droppedSpot = true;
				break;
			}
			const auto& light = spotView.get<SpotLightComponent>(entity);
			if (!light.Enabled) // per-light off: skip entirely (no light, no shadow tile)
			{
				continue;
			}
			const auto& transform = spotView.get<TransformComponent>(entity);

			const glm::mat3 rot = glm::mat3(transform.GetTransformMatrix());
			const glm::vec3 forward = SpotForward(rot);
			const float outer = SpotOuterRadians(light.InnerAngleDeg, light.OuterAngleDeg);

			// Assign a shadow atlas tile to this spot if it casts and shadows are globally enabled and a tile
			// is free (cap = ShadowPass::kMaxShadowSpots). ShadowIndex < 0 => unshadowed. The atlas is a
			// kSpotAtlasCols x kSpotAtlasCols grid; tile i sits at (col,row) with a 1/cols UV rect. The matrix
			// and rect are pure math here; RenderSystem renders each assigned tile and binds the atlas texture.
			int shadowIndex = -1;
			glm::mat4 shadowViewProj(1.0f);
			glm::vec4 atlasRect(0, 0, 1, 1);
			if (rtShadows && light.CastShadows)
			{
				// RT: mark as casting (shader traces to the spot); the atlas tile/matrix are unused, so use 0
				// as a sacrificial >= 0 sentinel. No tile budget => every caster gets a shadow.
				shadowIndex = 0;
			}
			else if (shadowsEnabled && light.CastShadows && nextShadowTile < ShadowPass::kMaxShadowSpots)
			{
				shadowIndex = nextShadowTile++;
				shadowViewProj = ShadowPass::ComputeSpotViewProj(transform.Position, forward, outer, light.Range);
				constexpr float inv = 1.0f / static_cast<float>(ShadowPass::kSpotAtlasCols);
				const int col = shadowIndex % static_cast<int>(ShadowPass::kSpotAtlasCols);
				const int row = shadowIndex / static_cast<int>(ShadowPass::kSpotAtlasCols);
				atlasRect = {static_cast<float>(col) * inv, static_cast<float>(row) * inv, inv, inv};
			}

			lightData.SpotLights[lightData.SpotCount++] =
			    PackSpotLight(transform.Position, light.Range, light.Color, light.Intensity, forward, light.InnerAngleDeg,
			                  light.OuterAngleDeg, shadowIndex, shadowViewProj, atlasRect);
		}
		if (droppedSpot && !m_WarnedDroppedSpot)
		{
			SS_CORE_WARN("More than {} spot lights in scene; extra lights ignored.", MAX_SPOT_LIGHTS);
		}
		m_WarnedDroppedSpot = droppedSpot;

		renderer3DSingleton.UploadLights(lightData);
	}

	void LightingSystem::DeclareAccess(SystemAccess& a) const
	{
		a.ReadComponents<DirectionalLightComponent, RotatorComponent, PointLightComponent, SpotLightComponent, TransformComponent, CameraComponent, CameraRuntimeComponent>().WriteResources<RendererService>();
	}
}
