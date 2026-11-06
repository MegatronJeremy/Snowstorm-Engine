#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <cstdint>

namespace Snowstorm
{
	enum class SpriteFilter : uint8_t
	{
		Nearest = 0,
		Linear = 1
	};

	// A textured quad drawn by Canvas2D cameras, in the entity's transform. Draw order is Layer, then Order,
	// then entity; position z never orders sprites.
	struct SpriteComponent
	{
		// 0 leaves View to game code, which is how a game shows a texture it writes itself.
		AssetHandle Texture{0};

		glm::vec2 Size{1.0f, 1.0f};  // world units, multiplied by the transform's scale
		glm::vec2 Pivot{0.5f, 0.5f}; // 0,0 = bottom left; flips mirror about it
		glm::vec4 Tint{1.0f};
		// Sub-rectangle of the texture as x, y, width, height in texels, origin top-left. A zero width or
		// height draws the whole texture; Size is not derived from it.
		glm::vec4 Rect{0.0f};
		bool FlipX = false;
		bool FlipY = false;
		int32_t Layer = 0;
		int32_t Order = 0;
		SpriteFilter Filter = SpriteFilter::Nearest;

		// Runtime only (not reflected, so never serialized): resolved from Texture by SpriteResolveSystem, or
		// set by game code. ResolvedFrom is the asset View came from, 0 when game code set it, so clearing
		// Texture drops a resolved view without touching an assigned one.
		Ref<TextureView> View;
		AssetHandle ResolvedFrom{0};
	};
}
