#include "LightingSystem.hpp"

#include "LightingComponents.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Render/RendererService.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Snowstorm
{
	void LightingSystem::Execute(Timestep ts)
	{
		auto lightView = View<DirectionalLightComponent>();
		auto& renderer3DSingleton = ServiceView<RendererService>();

		LightDataBlock lightData;
		bool droppedLights = false;
		// Primary sun = FIRST directional light (matches DirectionalLights[0] in the shader). Captured while
		// iterating so the shadow fit below uses the same light the shader treats as the sun.
		bool haveSun = false;
		glm::vec3 sunDir{0.0f};
		bool sunCasts = false;
		for (auto entity : lightView)
		{
			// LightDataBlock::Lights is a fixed GPUDirectionalLight[MAX_DIRECTIONAL_LIGHTS] mirrored on the
			// GPU; writing past it corrupts the adjacent LightCount/Padding and overruns the shader array.
			if (lightData.LightCount >= MAX_DIRECTIONAL_LIGHTS)
			{
				droppedLights = true;
				break;
			}

			auto& directionalLight = lightView.get<DirectionalLightComponent>(entity);
			if (!haveSun)
			{
				haveSun = true;
				sunDir = directionalLight.Direction;
				sunCasts = directionalLight.CastShadows;
			}
			lightData.Lights[lightData.LightCount++] = {
			    .Direction = glm::normalize(directionalLight.Direction),
			    .Intensity = directionalLight.Intensity,
			    .Color = directionalLight.Color,
			    .Padding{}};
		}

		if (droppedLights)
		{
			SS_CORE_WARN("More than {} directional lights in scene; extra lights ignored.", MAX_DIRECTIONAL_LIGHTS);
		}

		// Directional-sun shadow FIT (world -> light clip), the sun analogue of the per-spot fit computed
		// below. Kept here so ALL shadow setup lives in the light system; RenderSystem only binds the depth
		// resource + records the pass from this result. Gated by the global render.shadows kill-switch AND
		// the sun's authored CastShadows flag; ComputeSunViewProj also fails (Valid stays false) when the
		// scene has no renderable bounds, in which case RenderSystem leaves ShadowMapIndex 0 (fully lit).
		RendererService::SunShadowFit sunFit{};
		if (CVars::Shadows.Get() && haveSun && sunCasts)
		{
			sunFit.Valid = ShadowPass::ComputeSunViewProj(*m_World, sunDir, sunFit.LightViewProj);
		}
		renderer3DSingleton.SetSunShadowFit(sunFit);

		// Point lights: position from the entity transform (Unity/Unreal model -- the light carries no
		// position of its own). Joined with TransformComponent so an untransformed light is simply skipped.
		bool droppedPoint = false;
		for (auto pointView = View<PointLightComponent, TransformComponent>(); auto entity : pointView)
		{
			if (lightData.PointCount >= MAX_POINT_LIGHTS)
			{
				droppedPoint = true;
				break;
			}
			const auto& light = pointView.get<PointLightComponent>(entity);
			const auto& transform = pointView.get<TransformComponent>(entity);
			lightData.PointLights[lightData.PointCount++] = {
			    .Position = transform.Position,
			    .Range = light.Range,
			    .Color = light.Color,
			    .Intensity = light.Intensity};
		}
		if (droppedPoint)
		{
			SS_CORE_WARN("More than {} point lights in scene; extra lights ignored.", MAX_POINT_LIGHTS);
		}

		// Spot lights: position + forward (-Z) from the transform; cone half-angles stored as cosines so
		// the shader compares against dot() with no per-fragment trig. OuterAngle is clamped >= InnerAngle
		// so cos(inner) >= cos(outer) and the falloff denominator stays positive.
		const bool shadowsEnabled = CVars::Shadows.Get(); // global scalability kill-switch
		int nextShadowTile = 0;                           // next free atlas tile for a shadow-casting spot
		bool droppedSpot = false;
		for (auto spotView = View<SpotLightComponent, TransformComponent>(); auto entity : spotView)
		{
			if (lightData.SpotCount >= MAX_SPOT_LIGHTS)
			{
				droppedSpot = true;
				break;
			}
			const auto& light = spotView.get<SpotLightComponent>(entity);
			const auto& transform = spotView.get<TransformComponent>(entity);

			const glm::mat3 rot = glm::mat3(transform.GetTransformMatrix());
			const glm::vec3 forward = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));

			const float inner = glm::radians(light.InnerAngleDeg);
			const float outer = glm::radians(std::max(light.OuterAngleDeg, light.InnerAngleDeg));

			// Assign a shadow atlas tile to this spot if it casts and shadows are globally enabled and a tile
			// is free (cap = ShadowPass::kMaxShadowSpots). ShadowIndex < 0 => unshadowed. The atlas is a
			// kSpotAtlasCols x kSpotAtlasCols grid; tile i sits at (col,row) with a 1/cols UV rect. The matrix
			// and rect are pure math here; RenderSystem renders each assigned tile and binds the atlas texture.
			int shadowIndex = -1;
			glm::mat4 shadowViewProj(1.0f);
			glm::vec4 atlasRect(0, 0, 1, 1);
			if (shadowsEnabled && light.CastShadows && nextShadowTile < ShadowPass::kMaxShadowSpots)
			{
				shadowIndex = nextShadowTile++;
				shadowViewProj = ShadowPass::ComputeSpotViewProj(transform.Position, forward, outer, light.Range);
				constexpr float inv = 1.0f / static_cast<float>(ShadowPass::kSpotAtlasCols);
				const int col = shadowIndex % static_cast<int>(ShadowPass::kSpotAtlasCols);
				const int row = shadowIndex / static_cast<int>(ShadowPass::kSpotAtlasCols);
				atlasRect = {static_cast<float>(col) * inv, static_cast<float>(row) * inv, inv, inv};
			}

			lightData.SpotLights[lightData.SpotCount++] = {
			    .Position = transform.Position,
			    .Range = light.Range,
			    .Color = light.Color,
			    .Intensity = light.Intensity,
			    .Direction = forward,
			    .CosInner = std::cos(inner),
			    .CosOuter = std::cos(outer),
			    .ShadowIndex = shadowIndex,
			    .ShadowPad = {0, 0},
			    .ShadowViewProj = shadowViewProj,
			    .ShadowAtlasRect = atlasRect};
		}
		if (droppedSpot)
		{
			SS_CORE_WARN("More than {} spot lights in scene; extra lights ignored.", MAX_SPOT_LIGHTS);
		}

		renderer3DSingleton.UploadLights(lightData);
	}
}
