#pragma once

#include <span>

#include "Snowstorm/Components/CameraComponent.hpp"

namespace Snowstorm
{
	// The IBL bake and every shadow map are frame-global but only a Scene3D viewport samples them (#344).
	// `renderingPaths` holds the render path of each viewport that renders this frame; Canvas2D draws sprites
	// only. Zero rendering viewports also returns false: nothing would sample the result.
	inline bool NeedsSceneGlobalPasses(std::span<const CameraComponent::RenderPathType> renderingPaths)
	{
		for (const CameraComponent::RenderPathType path : renderingPaths)
		{
			if (path == CameraComponent::RenderPathType::Scene3D)
			{
				return true;
			}
		}
		return false;
	}
}
