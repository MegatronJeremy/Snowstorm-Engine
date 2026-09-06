#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	// A textured rectangle on the 2D canvas, composited over the viewport's final LDR image by the sprite
	// pass. Placement is in canvas units (render.sprite.canvas.*, default 1920x1080), top-left origin,
	// y down, independent of TransformComponent: a sprite is a screen-space element, not a scene object,
	// so a window of another size letterboxes the whole canvas rather than moving each sprite.
	struct SpriteComponent
	{
		Ref<TextureView> TextureInstance;

		glm::vec2 Position{0.0f}; // canvas units, top-left corner of the rect
		glm::vec2 Size{0.0f};     // canvas units; (0, 0) draws the texture at its own pixel size
		glm::vec4 UVRect{0.0f, 0.0f, 1.0f, 1.0f};
		glm::vec4 TintColor{1.0f};
		float RotationDeg = 0.0f; // about the rect centre, clockwise on screen
		int Layer = 0;            // higher draws later, so over lower
		bool Visible = true;
	};
}
