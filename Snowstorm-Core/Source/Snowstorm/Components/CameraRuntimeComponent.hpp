#pragma once

#include "Snowstorm/Math/Frustum.hpp"
#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	struct CameraRuntimeComponent
	{
		glm::mat4 Projection{1.0f};
		glm::mat4 View{1.0f};
		glm::mat4 ViewProjection{1.0f};
		glm::mat4 PrevViewProjection{1.0f}; // for TAA/motion vectors
		Frustum frustum;
	};
}
