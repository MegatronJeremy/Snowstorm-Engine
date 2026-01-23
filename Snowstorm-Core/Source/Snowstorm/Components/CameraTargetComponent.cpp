#include "CameraTargetComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterCameraTargetComponent()
	{
		using namespace rttr;

		registration::class_<CameraTargetComponent>("Snowstorm::CameraTargetComponent")
		.property("TargetViewportUUID", &CameraTargetComponent::TargetViewportUUID);

		Snowstorm::RegisterComponent<CameraTargetComponent>();
	}
}
