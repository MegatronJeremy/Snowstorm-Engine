#include "EnvironmentSystem.hpp"

#include "EnvironmentComponent.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Render/RendererService.hpp"

namespace Snowstorm
{
	void EnvironmentSystem::Execute(Timestep /*ts*/)
	{
		auto& renderer = ServiceView<RendererService>();

		// Use the first EnvironmentComponent found. A scene need not author one: the default-constructed
		// block is inactive (no sky pass, zeroed ambient), so surfaces are lit by direct lights only —
		// the component is opt-in. Fail-soft, no warning: a missing environment is a valid scene.
		EnvironmentDataBlock env{};
		for (auto envView = View<EnvironmentComponent>(); const auto entity : envView)
		{
			const auto& ec = envView.get<EnvironmentComponent>(entity);
			env.SkyZenithColor = ec.SkyZenithColor;
			env.SkyHorizonColor = ec.SkyHorizonColor;
			env.GroundColor = ec.GroundColor;
			env.SkyIntensity = ec.SkyIntensity;
			env.DrawProceduralSky = (ec.Background == BackgroundMode::ProceduralSky);
			break;
		}

		renderer.UploadEnvironment(env);
	}
}
