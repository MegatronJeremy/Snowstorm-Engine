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
		    .property("TilingFactor", &SpriteComponent::TilingFactor)
		    .property("TintColor", &SpriteComponent::TintColor)(metadata("Color", true)); // inspector: color picker
	}

	AUTO_REGISTER_COMPONENT(SpriteComponent);
}
