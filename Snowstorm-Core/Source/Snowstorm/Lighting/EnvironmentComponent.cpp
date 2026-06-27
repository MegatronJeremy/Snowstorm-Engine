#include "EnvironmentComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::enumeration<BackgroundMode>("Snowstorm::BackgroundMode")(
		    value("SolidColor", BackgroundMode::SolidColor),
		    value("ProceduralSky", BackgroundMode::ProceduralSky));

		registration::class_<EnvironmentComponent>("Snowstorm::EnvironmentComponent")
		    .property("Background", &EnvironmentComponent::Background)
		    .property("SkyZenithColor", &EnvironmentComponent::SkyZenithColor)(metadata("Color", true))
		    .property("SkyHorizonColor", &EnvironmentComponent::SkyHorizonColor)(metadata("Color", true))
		    .property("GroundColor", &EnvironmentComponent::GroundColor)(metadata("Color", true))
		    .property("SkyIntensity", &EnvironmentComponent::SkyIntensity)(metadata("Min", 0.0f), metadata("Speed", 0.01f));
	}

	AUTO_REGISTER_COMPONENT(EnvironmentComponent);
}
