#include "ViewportComponent.hpp"

#include <rttr/registration.h>

#include "ComponentRegistry.hpp"

namespace Snowstorm
{
	void RegisterViewportComponent()
	{
		using namespace rttr;

		registration::class_<ViewportComponent>("Snowstorm::ViewportComponent")
		.property("Size", &ViewportComponent::Size);

		Snowstorm::RegisterComponent<ViewportComponent>();
	}
}
