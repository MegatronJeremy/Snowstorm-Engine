#include "VisibilityComponents.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	void RegisterVisibilityComponents()
	{
		using namespace rttr;

		registration::class_<VisibilityComponent>("Snowstorm::VisibilityComponent")
			.property("Mask", &VisibilityComponent::Mask);

		registration::class_<CameraVisibilityComponent>("Snowstorm::CameraVisibilityComponent")
			.property("Mask", &CameraVisibilityComponent::Mask);

		Snowstorm::RegisterComponent<VisibilityComponent>();
		Snowstorm::RegisterComponent<CameraVisibilityComponent>();
	}
}
