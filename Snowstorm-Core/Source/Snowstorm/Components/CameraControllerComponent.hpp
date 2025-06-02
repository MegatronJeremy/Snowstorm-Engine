#pragma once

namespace Snowstorm
{
	struct CameraControllerComponent
	{
		bool RotationEnabled = true;
		float ZoomSpeed = 5.0f;
		float MoveSpeed = 5.0f;
		float RotationSpeed = 180.0f; //-- degrees per second
		float LookSensitivity = 1.0f; //-- degrees per pixel moved
	};

	void RegisterCameraControllerComponent();
}
