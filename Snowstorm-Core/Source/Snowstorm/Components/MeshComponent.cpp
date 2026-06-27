#include "MeshComponent.hpp"

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<MeshComponent>("Snowstorm::MeshComponent")
		    .constructor()
		    .property("Mesh", &MeshComponent::MeshHandle)(
		        metadata("AssetType", static_cast<int>(AssetType::Mesh)) // inspector asset picker filter
		    );
	}

	AUTO_REGISTER_COMPONENT(MeshComponent);
}
