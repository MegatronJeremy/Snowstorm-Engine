#include "MaterialComponent.hpp"

#include "ComponentRegistry.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<MaterialComponent>("Snowstorm::MaterialComponent")
		    .property("Material", &MaterialComponent::Material)(
		        metadata("AssetType", static_cast<int>(AssetType::Material)) // inspector asset picker filter
		    );
	}

	AUTO_REGISTER_COMPONENT(MaterialComponent);
}
