#pragma once

namespace Snowstorm
{
	struct CameraControllerComponent
	{
		bool RotationEnabled = true;
		float ZoomSpeed = 2.5f; //-- world units dollied per scroll notch (perspective); eased via ZoomSmoothing
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
		//-- Scroll-zoom glide decay (1/sec). Each scroll notch adds an impulse to a zoom velocity that
		//-- coasts and decays at this rate, so dollying feels smooth instead of stepping per notch.
		//-- Higher = snappier/shorter glide, lower = floatier. Zero = instant (old per-notch behavior).
		float ZoomSmoothing = 12.0f;
	};
}
