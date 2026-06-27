#include "CameraTargetComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<CameraTargetComponent>("Snowstorm::CameraTargetComponent")
		    .property("TargetViewportUUID", &CameraTargetComponent::TargetViewportUUID);
	}

	AUTO_REGISTER_COMPONENT(CameraTargetComponent);
}
