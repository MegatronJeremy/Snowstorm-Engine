#include "LightingComponents.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterLightingComponents()
	{
		using namespace rttr;

		registration::class_<DirectionalLightComponent>("Snowstorm::DirectionalLightComponent")
			.property("Direction", &DirectionalLightComponent::Direction)
			.property("Color", &DirectionalLightComponent::Color)
			.property("Intensity", &DirectionalLightComponent::Intensity);

		Snowstorm::RegisterComponent<DirectionalLightComponent>();
	}
}
