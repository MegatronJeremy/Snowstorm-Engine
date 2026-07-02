#include "SpriteComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<SpriteComponent>("Snowstorm::SpriteComponent")
		    .constructor()
		    .property("TilingFactor", &SpriteComponent::TilingFactor)(metadata("Min", 0.0f), metadata("Speed", 0.1f))
		    .property("TintColor", &SpriteComponent::TintColor)(metadata("Color", true)); // inspector: color picker
	}

	AUTO_REGISTER_COMPONENT(SpriteComponent);
}
