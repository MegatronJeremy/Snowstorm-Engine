#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	struct SpriteComponent
	{
		Ref<TextureView> TextureInstance;
		float TilingFactor = 1.0f;
		glm::vec4 TintColor = glm::vec4{1.0f};
	};
}
