#include "CameraTargetComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<CameraTargetComponent>("Snowstorm::CameraTargetComponent")
		    .property("TargetViewportUUID", &CameraTargetComponent::TargetViewportUUID)(
		        metadata("JsonAlias", "TargetViewport") // key written before scenes used reflected names
		    );
	}

	AUTO_REGISTER_COMPONENT(CameraTargetComponent);
}
