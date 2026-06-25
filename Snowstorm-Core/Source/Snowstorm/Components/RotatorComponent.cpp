#include "RotatorComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterRotatorComponent()
	{
		using namespace rttr;

		registration::class_<RotatorComponent>("Snowstorm::RotatorComponent")
		    .constructor()
		    .property("Axis", &RotatorComponent::Axis)
		    .property("SpeedDegPerSec", &RotatorComponent::SpeedDegPerSec);

		Snowstorm::RegisterComponent<RotatorComponent>();
	}
}
