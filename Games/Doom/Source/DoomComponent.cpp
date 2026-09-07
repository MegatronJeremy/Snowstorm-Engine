#include "DoomComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	RTTR_REGISTRATION
	{
		registration::class_<DoomComponent>("DoomComponent")
		    .property("Material", &DoomComponent::Material);
	}

	AUTO_REGISTER_COMPONENT(DoomComponent);
}
