#include "LightingComponents.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<DirectionalLightComponent>("Snowstorm::DirectionalLightComponent")
		    .property("Direction", &DirectionalLightComponent::Direction)
		    .property("Color", &DirectionalLightComponent::Color)(metadata("Color", true)) // inspector: color picker
		    .property("Intensity", &DirectionalLightComponent::Intensity)(metadata("Min", 0.0f), metadata("Speed", 0.01f))
		    .property("CastShadows", &DirectionalLightComponent::CastShadows); // inspector: checkbox
	}

	AUTO_REGISTER_COMPONENT(DirectionalLightComponent);
}
