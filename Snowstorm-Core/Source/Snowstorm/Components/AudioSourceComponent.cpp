#include "AudioSourceComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	RTTR_REGISTRATION
	{
		registration::class_<AudioSourceComponent>("AudioSourceComponent")
		    .property("Clip", &AudioSourceComponent::Clip)(
		        metadata("AssetType", static_cast<int>(AssetType::Audio)) // inspector asset picker filter
		        )
		    .property("Volume", &AudioSourceComponent::Volume)
		    .property("Pitch", &AudioSourceComponent::Pitch)
		    .property("Loop", &AudioSourceComponent::Loop)
		    .property("PlayOnStart", &AudioSourceComponent::PlayOnStart);
	}

	AUTO_REGISTER_COMPONENT(AudioSourceComponent);
}
