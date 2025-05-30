#pragma once

#include "Snowstorm/World/SceneCamera.h"

#include <rttr/registration.h>

namespace Snowstorm
{
	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true; // TODO: think about moving to scene
		bool FixedAspectRatio = false;

		RTTR_ENABLE()
	};
}
