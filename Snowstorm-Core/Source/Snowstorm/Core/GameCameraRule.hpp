#pragma once

#include <string>

#include <glm/glm.hpp>

#include "Snowstorm/Core/EngineCVars.hpp"

namespace Snowstorm
{
	struct GameCameraPlan
	{
		// False: no Primary camera, nothing renders to the swapchain and the caller warns.
		bool HasCamera = false;
		// A bare authored camera may carry only a CameraComponent; the controller drives its Transform.
		bool AddTransform = false;
		bool ApplyOverride = false;
		glm::vec3 OverridePosition{0.0f};
		glm::vec3 OverrideRotation{0.0f};
	};

	// camera.override outranks the authored pose but never the choice of camera: with no Primary camera it
	// is not consulted, so a harness cannot conjure a view the scene did not author.
	[[nodiscard]] inline GameCameraPlan DecideGameCamera(const bool foundPrimary, const bool hasTransform,
	                                                     const std::string& cameraOverride)
	{
		GameCameraPlan plan;
		if (!foundPrimary)
		{
			return plan;
		}
		plan.HasCamera = true;
		plan.AddTransform = !hasTransform;
		plan.ApplyOverride = CVars::ParseCameraOverride(cameraOverride, plan.OverridePosition, plan.OverrideRotation);
		return plan;
	}
}
