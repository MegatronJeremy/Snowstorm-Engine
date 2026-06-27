#include "TagComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<TagComponent>("Snowstorm::TagComponent")
		    .constructor()
		    .property("Tag", &TagComponent::Tag);
	}

	AUTO_REGISTER_COMPONENT(TagComponent);
}
