#include "MaterialComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	void RegisterMaterialComponent()
	{
		registration::class_<MaterialComponent>("Snowstorm::MaterialComponent")
		.property("Material", &MaterialComponent::Material); // TODO material editor in the future?

		Snowstorm::RegisterComponent<MaterialComponent>();
	}
}
