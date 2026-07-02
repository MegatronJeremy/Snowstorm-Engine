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
		    .property("ZoomSpeed", &CameraControllerComponent::ZoomSpeed)(metadata("Min", 0.0f))
		    // Multipliers scale MoveSpeed; a negative one would invert movement. Sprint >= 1 (speeds up),
		    // slow in [0,1] (slows down) -- both non-negative.
		    .property("SprintMultiplier", &CameraControllerComponent::SprintMultiplier)(metadata("Min", 1.0f), metadata("Speed", 0.05f))
		    .property("SlowMultiplier", &CameraControllerComponent::SlowMultiplier)(metadata("Min", 0.0f), metadata("Max", 1.0f), metadata("Speed", 0.01f))
		    // Geometric speed step: a value < 1 would shrink fly speed on scroll-up. Keep it >= 1.
		    .property("SpeedAdjustStep", &CameraControllerComponent::SpeedAdjustStep)(metadata("Min", 1.0f), metadata("Speed", 0.01f))
		    .property("MinMoveSpeed", &CameraControllerComponent::MinMoveSpeed)(metadata("Min", 0.0f))
		    .property("MaxMoveSpeed", &CameraControllerComponent::MaxMoveSpeed)(metadata("Min", 0.0f))
		    // Exponential smoothing rates (1/sec); 0 disables smoothing on that channel, negative is invalid.
		    .property("LookSmoothing", &CameraControllerComponent::LookSmoothing)(metadata("Min", 0.0f))
		    .property("MoveSmoothing", &CameraControllerComponent::MoveSmoothing)(metadata("Min", 0.0f));
	}

	AUTO_REGISTER_COMPONENT(CameraControllerComponent);
}
