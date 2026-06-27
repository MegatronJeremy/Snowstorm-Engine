#include "TransformComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<TransformComponent>("Snowstorm::TransformComponent")
		    .constructor()
		    .property("Position", &TransformComponent::Position)
		    .property("Rotation", &TransformComponent::Rotation)
		    .property("Scale", &TransformComponent::Scale);
	}

	AUTO_REGISTER_COMPONENT(TransformComponent);
}
