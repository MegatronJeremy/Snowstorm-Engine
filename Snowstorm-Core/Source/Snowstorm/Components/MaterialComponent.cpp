#include "MaterialComponent.hpp"

#include "ComponentRegistry.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	void RegisterMaterialComponent()
	{
		registration::class_<MaterialComponent>("Snowstorm::MaterialComponent")
		    .property("Material", &MaterialComponent::Material)(
		        metadata("AssetType", static_cast<int>(AssetType::Material)) // inspector asset picker filter
		    );

		Snowstorm::RegisterComponent<MaterialComponent>();
	}
}
