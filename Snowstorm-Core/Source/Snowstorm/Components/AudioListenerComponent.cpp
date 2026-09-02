#include "AudioListenerComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	// The pose itself comes from the entity's TransformComponent, so Enabled is the only authored field.
	RTTR_REGISTRATION
	{
		registration::class_<AudioListenerComponent>("AudioListenerComponent")
		    .property("Enabled", &AudioListenerComponent::Enabled);
	}

	AUTO_REGISTER_COMPONENT(AudioListenerComponent);
}
