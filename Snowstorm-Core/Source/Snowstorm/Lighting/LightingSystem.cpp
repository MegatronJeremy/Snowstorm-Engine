#include "LightingSystem.hpp"

#include "LightingComponents.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Render/RendererSingleton.hpp"

namespace Snowstorm
{
	void LightingSystem::Execute(Timestep ts)
	{
		auto lightView = View<DirectionalLightComponent>();
		auto& renderer3DSingleton = SingletonView<RendererSingleton>();

		LightDataBlock lightData;
		for (auto entity : lightView)
		{
			auto& directionalLight = lightView.get<DirectionalLightComponent>(entity);
			lightData.Lights[lightData.LightCount++] = {
				.Direction = glm::normalize(directionalLight.Direction),
				.Intensity = directionalLight.Intensity,
				.Color = directionalLight.Color,
				.Padding{}
			};
		}

		renderer3DSingleton.UploadLights(lightData);
	}
}
