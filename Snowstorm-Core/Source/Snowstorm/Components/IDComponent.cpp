#include "IDComponent.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<IDComponent>("Snowstorm::IDComponent")
		    .property("Id", &IDComponent::Id);

		registration::class_<UUID>("Snowstorm::UUID")
		    .property_readonly("Value", &UUID::ToString);
	}

	AUTO_REGISTER_COMPONENT(IDComponent);
}
