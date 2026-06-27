#include "LightingSystem.hpp"

#include "LightingComponents.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"

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

		renderer3DSingleton.UploadLights(lightData);
	}
}
