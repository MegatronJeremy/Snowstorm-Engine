#include "NativeScriptComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<NativeScriptComponent>("Snowstorm::NativeScriptComponent")
		    .constructor()
		    .property("Instance", &NativeScriptComponent::Instance);
	}

	AUTO_REGISTER_COMPONENT(NativeScriptComponent);
}
