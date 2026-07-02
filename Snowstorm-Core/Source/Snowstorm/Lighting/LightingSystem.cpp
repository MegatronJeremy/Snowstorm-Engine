#include "LightingSystem.hpp"

#include "LightingComponents.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Snowstorm
{
	void LightingSystem::Execute(Timestep ts)
	{
		auto lightView = View<DirectionalLightComponent>();
		auto& renderer3DSingleton = SingletonView<RendererSingleton>();

		LightDataBlock lightData;
		bool droppedLights = false;
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

			lightData.SpotLights[lightData.SpotCount++] = {
			    .Position = transform.Position,
			    .Range = light.Range,
			    .Color = light.Color,
			    .Intensity = light.Intensity,
			    .Direction = forward,
			    .CosInner = std::cos(inner),
			    .CosOuter = std::cos(outer),
			    .Padding = {}};
		}
		if (droppedSpot)
		{
			SS_CORE_WARN("More than {} spot lights in scene; extra lights ignored.", MAX_SPOT_LIGHTS);
		}

		renderer3DSingleton.UploadLights(lightData);
	}
}
