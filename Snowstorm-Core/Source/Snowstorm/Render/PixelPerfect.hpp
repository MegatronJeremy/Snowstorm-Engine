#pragma once

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	// Pixel-perfect 2D camera math (#340). A texel is 1/PixelsPerUnit world units; the camera is
	// pixel-perfect when every texel spans the same whole number of screen pixels.

	// Largest integer factor at which the reference resolution fits the viewport, at least 1 so a viewport
	// smaller than the reference still renders (cropped) rather than collapsing. A non-positive reference
	// axis does not constrain; with no constraint at all the factor is 1.
	inline int PixelPerfectScale(float viewportW, float viewportH, float referenceW, float referenceH)
	{
		int scale = 0;
		const auto fit = [&scale](float viewport, float reference)
		{
			if (reference < 1.0f)
				return;
			const int s = std::max(1, static_cast<int>(std::floor(viewport / reference)));
			scale = scale == 0 ? s : std::min(scale, s);
		};
		fit(viewportW, referenceW);
		fit(viewportH, referenceH);
		return std::max(scale, 1);
	}

	// Full view height in world units at which one texel covers exactly `scale` screen pixels.
	inline float PixelPerfectOrthographicSize(float viewportH, float pixelsPerUnit, int scale)
	{
		return viewportH / (pixelsPerUnit * static_cast<float>(std::max(scale, 1)));
	}

	// Nearest texel-grid position along one axis.
	inline float SnapToTexel(float worldCoord, float pixelsPerUnit)
	{
		return std::round(worldCoord * pixelsPerUnit) / pixelsPerUnit;
	}
}
