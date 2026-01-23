#include "MeshComponent.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterMeshComponent()
	{
		using namespace rttr;

		registration::class_<MeshComponent>("Snowstorm::MeshComponent")
			.constructor()
			.property("Mesh", &MeshComponent::MeshHandle);
		Snowstorm::RegisterComponent<MeshComponent>();
	}
}
