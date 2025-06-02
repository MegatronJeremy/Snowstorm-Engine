#pragma once

#include "Snowstorm/World/SceneCamera.h"

namespace Snowstorm
{
	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true; // TODO: think about moving to scene
		bool FixedAspectRatio = false;
	};

	void RegisterCameraComponent();
}
