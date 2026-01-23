#pragma once

#include <utility>

namespace Snowstorm
{
	// Runtime-only state for CameraControllerSystem.
	struct CameraControllerRuntimeComponent
	{
		bool WasRightClickHeld = false;
	};
}
