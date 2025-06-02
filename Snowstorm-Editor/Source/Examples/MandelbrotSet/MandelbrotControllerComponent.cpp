#include "MandelbrotControllerComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	RTTR_REGISTRATION
	{
		registration::class_<MandelbrotControllerComponent>("MandelbrotControllerComponent")
			.property("Center", &MandelbrotControllerComponent::Center)
			.property("Zoom", &MandelbrotControllerComponent::Zoom)
			.property("MaxIterations", &MandelbrotControllerComponent::MaxIterations);
	}

	AUTO_REGISTER_COMPONENT(MandelbrotControllerComponent);
}
