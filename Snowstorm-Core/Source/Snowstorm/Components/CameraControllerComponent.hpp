#pragma once

namespace Snowstorm
{
	struct CameraControllerComponent
	{
		bool RotationEnabled = true;
		float ZoomSpeed = 5.0f;
		float MoveSpeed = 5.0f;
		float LookSensitivity = 0.1f; //-- degrees per pixel moved (1:1 mouse look)

		//-- Sprint/slow multipliers applied to MoveSpeed while the modifier is held.
		float SprintMultiplier = 4.0f; //-- Shift
		float SlowMultiplier = 0.25f;  //-- Ctrl

		//-- Scroll (while RMB held) adjusts fly speed geometrically: each notch multiplies
		//-- MoveSpeed by this factor, clamped to [MinMoveSpeed, MaxMoveSpeed].
		float SpeedAdjustStep = 1.1f;
		float MinMoveSpeed = 0.1f;
		float MaxMoveSpeed = 500.0f;

		//-- Exponential smoothing rates (1/sec). Higher = snappier, lower = floatier.
		//-- Zero disables smoothing on that channel.
		float LookSmoothing = 30.0f;
		float MoveSmoothing = 15.0f;
	};

	void RegisterCameraControllerComponent();
}
