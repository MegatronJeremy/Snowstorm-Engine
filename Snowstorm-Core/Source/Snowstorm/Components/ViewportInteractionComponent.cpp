#include "ViewportInteractionComponent.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<ViewportInteractionComponent>("Snowstorm::ViewportInteractionComponent")
		    .property_readonly("Hovered", &ViewportInteractionComponent::Hovered)
		    .property_readonly("Focused", &ViewportInteractionComponent::Focused);
	}

	AUTO_REGISTER_COMPONENT(ViewportInteractionComponent);
}
