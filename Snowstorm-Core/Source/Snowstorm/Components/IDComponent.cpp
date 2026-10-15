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

	namespace
	{
		// Identity and name live in the entity header of the scene file, not in "Components".
		struct AutoRegister_IDComponent
		{
			AutoRegister_IDComponent()
			{
				ComponentRegisterOptions opts{};
				opts.Structural = true;
				RegisterComponent<IDComponent>(opts);
			}
		};
		const AutoRegister_IDComponent s_autoRegister_IDComponent;
	}
}
