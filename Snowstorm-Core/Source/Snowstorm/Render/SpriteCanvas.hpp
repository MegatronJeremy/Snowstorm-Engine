#pragma once

#include "Snowstorm/Math/Math.hpp"

#include <algorithm>
#include <cstdint>

namespace Snowstorm
{
	// One sprite as the GPU sees it. Mirrors SpriteInstance in Engine/Shaders/Include/Sprite.hlsli
	// field-for-field; the static_assert below pins the 64-byte stride the StructuredBuffer stride assumes.
	struct SpriteInstance
	{
		glm::vec4 Rect{0.0f};                     // x, y, width, height in canvas units; top-left origin
		glm::vec4 UVRect{0.0f, 0.0f, 1.0f, 1.0f}; // u0, v0, u1, v1
		glm::vec4 Tint{1.0f};                     // linear RGBA
		uint32_t TextureIndex = 0;                // bindless index; 0 is the engine's white texture
		float Rotation = 0.0f;                    // radians about the rect centre
		glm::vec2 _Pad{0.0f};
	};
	static_assert(sizeof(SpriteInstance) == 64, "SpriteInstance must match the HLSL struct (64 bytes)");

	// Mirrors SpriteCB in Sprite.hlsli field-for-field (two 16-byte rows).
	struct SpriteCanvasConstants
	{
		glm::vec2 TargetSize{0.0f};
		float CanvasScale = 1.0f;
		float _Pad0 = 0.0f;
		glm::vec2 CanvasOffset{0.0f};
		glm::vec2 _Pad1{0.0f};
	};
	static_assert(sizeof(SpriteCanvasConstants) == 32, "SpriteCanvasConstants must match SpriteCB (32 bytes)");

	// How a design-resolution canvas sits in a target of another size: the largest uniform scale that
	// fits, centred, so the game is authored once at one resolution and letterboxes elsewhere instead of
	// stretching. A zero-sized target or canvas yields scale 0, which draws nothing rather than dividing.
	struct CanvasFit
	{
		float Scale = 0.0f;
		glm::vec2 Offset{0.0f};
	};

	[[nodiscard]] inline CanvasFit FitCanvas(const uint32_t targetWidth, const uint32_t targetHeight,
	                                         const glm::vec2 designSize)
	{
		if (targetWidth == 0 || targetHeight == 0 || designSize.x <= 0.0f || designSize.y <= 0.0f)
		{
			return {};
		}
		const float tw = static_cast<float>(targetWidth);
		const float th = static_cast<float>(targetHeight);
		const float scale = std::min(tw / designSize.x, th / designSize.y);
		const glm::vec2 fitted = designSize * scale;
		return {scale, {(tw - fitted.x) * 0.5f, (th - fitted.y) * 0.5f}};
	}

	[[nodiscard]] inline SpriteCanvasConstants MakeCanvasConstants(const uint32_t targetWidth,
	                                                               const uint32_t targetHeight,
	                                                               const glm::vec2 designSize)
	{
		const CanvasFit fit = FitCanvas(targetWidth, targetHeight, designSize);
		SpriteCanvasConstants c{};
		c.TargetSize = {static_cast<float>(targetWidth), static_cast<float>(targetHeight)};
		c.CanvasScale = fit.Scale;
		c.CanvasOffset = fit.Offset;
		return c;
	}
}
