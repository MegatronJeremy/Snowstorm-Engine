#include "CameraControllerComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;

		registration::class_<CameraControllerComponent>("Snowstorm::CameraControllerComponent")
		    .property("LookSensitivity", &CameraControllerComponent::LookSensitivity)(metadata("Min", 0.0f), metadata("Speed", 0.01f))
		    .property("MoveSpeed", &CameraControllerComponent::MoveSpeed)(metadata("Min", 0.0f))
		    .property("RotationEnabled", &CameraControllerComponent::RotationEnabled)
		    .property("ZoomSpeed", &CameraControllerComponent::ZoomSpeed)
		    .property("SprintMultiplier", &CameraControllerComponent::SprintMultiplier)
		    .property("SlowMultiplier", &CameraControllerComponent::SlowMultiplier)
		    .property("SpeedAdjustStep", &CameraControllerComponent::SpeedAdjustStep)
		    .property("MinMoveSpeed", &CameraControllerComponent::MinMoveSpeed)
		    .property("MaxMoveSpeed", &CameraControllerComponent::MaxMoveSpeed)
		    .property("LookSmoothing", &CameraControllerComponent::LookSmoothing)
		    .property("MoveSmoothing", &CameraControllerComponent::MoveSmoothing);
	}

	AUTO_REGISTER_COMPONENT(CameraControllerComponent);
}
