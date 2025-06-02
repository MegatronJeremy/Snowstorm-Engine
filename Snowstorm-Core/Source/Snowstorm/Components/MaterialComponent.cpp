#include "MaterialComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	using namespace rttr;

	void RegisterMaterialComponent()
	{
		registration::class_<MaterialComponent>("Snowstorm::MaterialComponent")
			.property("MaterialInstance", &MaterialComponent::MaterialInstance);

		registration::class_<Material>("Snowstorm::Material")
			.property("Color", &Material::GetColor, &Material::SetColor)
			.property("ShaderPath", &Material::GetShaderPath, &Material::SetShaderPath);

		Snowstorm::RegisterComponent<MaterialComponent>();
	}
}
