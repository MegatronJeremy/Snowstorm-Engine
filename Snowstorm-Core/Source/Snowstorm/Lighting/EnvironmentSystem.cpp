#include "EnvironmentSystem.hpp"

#include "EnvironmentComponent.hpp"
#include "LightingUniforms.hpp"

#include "Snowstorm/Render/RendererService.hpp"

namespace Snowstorm
{
	void EnvironmentSystem::Execute(Timestep /*ts*/)
	{
		auto& renderer = ServiceView<RendererService>();

		// Use the first EnvironmentComponent the scene authors. If none exists, fall back to a built-in
		// DEFAULT environment (procedural sky on, the component's own default colors) rather than the
		// zeroed/inactive block — so an empty or still-loading world shows the sky instead of a black
		// viewport. This mirrors a production engine's implicit default environment (Godot's default
		// WorldEnvironment / Unity's default skybox): geometry streams in *over* a sky, never over black.
		// A scene that authors the component still overrides this. The default here is a default-constructed
		// EnvironmentComponent, so the fallback tracks the authored defaults and can't drift from them.
		EnvironmentComponent source{};
		for (auto envView = View<EnvironmentComponent>(); const auto entity : envView)
		{
			source = envView.get<EnvironmentComponent>(entity);
			break;
		}

		EnvironmentDataBlock env{};
		env.SkyZenithColor = source.SkyZenithColor;
		env.SkyHorizonColor = source.SkyHorizonColor;
		env.GroundColor = source.GroundColor;
		env.SkyIntensity = source.SkyIntensity;
		env.DrawProceduralSky = (source.Background == BackgroundMode::ProceduralSky);

		renderer.UploadEnvironment(env);
	}
}
