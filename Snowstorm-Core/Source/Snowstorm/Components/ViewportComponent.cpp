#include "ViewportComponent.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<ViewportComponent>("Snowstorm::ViewportComponent")
		    .property("Size", &ViewportComponent::Size);
	}

	AUTO_REGISTER_COMPONENT(ViewportComponent);
}
