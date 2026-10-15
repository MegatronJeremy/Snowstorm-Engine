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

	namespace
	{
		// Identity and name live in the entity header of the scene file, not in "Components".
		struct AutoRegister_TagComponent
		{
			AutoRegister_TagComponent()
			{
				ComponentRegisterOptions opts{};
				opts.Structural = true;
				RegisterComponent<TagComponent>(opts);
			}
		};
		const AutoRegister_TagComponent s_autoRegister_TagComponent;
	}
}
