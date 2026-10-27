#pragma once

#include <glm/glm.hpp>

namespace Snowstorm
{
	struct CameraJitter
	{
		glm::mat4 JitteredViewProjection{1.0f};
		glm::vec2 Ndc{0.0f};
	};

	// Dataset export forces jitter on only when dataset.jitter is set (a spatial net trains unjittered);
	// compare mode forces it off except under a temporal resolve (TAA/DLAA), which needs the sub-pixel samples.
	inline bool JitterEnabled(const bool datasetExport, const bool datasetJitter, const bool taaActive, const bool jitter, const bool compare)
	{
		return (datasetExport && datasetJitter) || taaActive || (jitter && !compare);
	}

	// NDC spans 2 units across the full target, so the offset is in render-target pixels, not output pixels.
	inline glm::vec2 JitterPixelsToNdc(const glm::vec2 pixels, const float renderWidth, const float renderHeight)
	{
		return {pixels.x * 2.0f / renderWidth, pixels.y * 2.0f / renderHeight};
	}

	// Adding to the z column's x/y rows scales with w, so the shift survives the perspective divide as a
	// constant NDC offset at every depth: -Ndc, since a right-handed projection has
	// w = -z_view. Only the color pass reads the result; `viewProjection` stays canonical for motion vectors and
	// culling. A target under 1 px (not resolved yet, mid-resize) stays unjittered.
	inline CameraJitter InjectJitter(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& viewProjection,
	                                 const glm::vec2 jitterPixels, const float renderWidth, const float renderHeight)
	{
		if (renderWidth < 1.0f || renderHeight < 1.0f)
			return {viewProjection, glm::vec2(0.0f)};

		const glm::vec2 ndc = JitterPixelsToNdc(jitterPixels, renderWidth, renderHeight);
		glm::mat4 jittered = projection;
		jittered[2][0] += ndc.x;
		jittered[2][1] += ndc.y;
		return {jittered * view, ndc};
	}
}
