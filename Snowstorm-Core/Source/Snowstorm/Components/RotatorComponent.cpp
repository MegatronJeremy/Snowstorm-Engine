#include "RotatorComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<RotatorComponent>("Snowstorm::RotatorComponent")
		    .constructor()
		    .property("Axis", &RotatorComponent::Axis)
		    .property("SpeedDegPerSec", &RotatorComponent::SpeedDegPerSec);
	}

	AUTO_REGISTER_COMPONENT(RotatorComponent);
}
