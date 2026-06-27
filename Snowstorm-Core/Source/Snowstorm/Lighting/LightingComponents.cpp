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
		    .property("Color", &DirectionalLightComponent::Color)
		    .property("Intensity", &DirectionalLightComponent::Intensity);
	}

	AUTO_REGISTER_COMPONENT(DirectionalLightComponent);
}
