#pragma once

#include "Snowstorm/Math/Frustum.hpp"
#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	struct CameraRuntimeComponent
	{
		glm::mat4 Projection{1.0f};
		glm::mat4 View{1.0f};
		glm::mat4 ViewProjection{1.0f};     // UNJITTERED truth — motion vectors + frustum culling read this
		glm::mat4 PrevViewProjection{1.0f}; // last frame's unjittered VP (motion vectors, #44)

		// Temporal sub-pixel jitter (#44). CameraJitterSystem fills these each frame; the forward COLOR
		// pass uploads JitteredViewProjection into FrameCB, while velocity/shadow/culling keep using the
		// unjittered ViewProjection above. JitterNdc is this frame's offset in clip/NDC units (the temporal
		// resolve next increment needs it to un-jitter history). When render.jitter is off, the jitter
		// system sets JitteredViewProjection == ViewProjection and JitterNdc == 0 (a clean no-op).
		glm::mat4 JitteredViewProjection{1.0f};
		glm::vec2 JitterNdc{0.0f};

		Frustum frustum; // built from the unjittered ViewProjection
	};
}
