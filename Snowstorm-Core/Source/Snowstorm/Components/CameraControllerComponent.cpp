#include "CameraControllerComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	void RegisterCameraControllerComponent()
	{
		using namespace rttr;

		registration::class_<CameraControllerComponent>("Snowstorm::CameraControllerComponent")
			.property("LookSensitivity", &CameraControllerComponent::LookSensitivity)
			.property("MoveSpeed", &CameraControllerComponent::MoveSpeed)
			.property("RotationEnabled", &CameraControllerComponent::RotationEnabled)
			.property("RotationSpeed", &CameraControllerComponent::RotationSpeed)
			.property("ZoomSpeed", &CameraControllerComponent::ZoomSpeed);

		Snowstorm::RegisterComponent<CameraControllerComponent>();
	}
}
